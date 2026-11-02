# Altvane Dialogue architecture

Paths are relative to `products/dialogue/`. Everything here is Altvane code except the models (`models/README.md`)
and PFFFT.

## Signal path

```
host rate ─► RateBridge: down to 48 kHz ─► Engine ─► up to the host rate ─► + g·(x_delayed − round trip)
                                            │
             audio thread: copy into the input ring, copy out of the output ring (cross-fading to the fallback if late)
                                            │
             worker thread, per 10 ms hop:  STFT (960 Vorbis, 480 hop) ─► model ─► stem shares ─► transfer ─► iSTFT
```

- `dsp/spectral` — the STFT constants, the Vorbis window and a 960-point real FFT (PFFFT: 960 = 2⁶·3·5).
- `dsp/separator` — `SeparatorModel`, one network per quality shared by every instance, and `SeparatorStreams`,
  the recurrent state of one or two streams (the mid in Linked mode, each channel in Stereo, the mid and the side in
  Mid/Side), preallocated and run together. The model returns the speech estimate of the frame `kModelLag` (4) hops before the one it was given.
- `dsp/dpdfnet`, `dsp/onnx_file`, `dsp/kernels` — DPDFNet's streaming network, written out in C++, with its weights
  read from the embedded ONNX file. Every stream of a frame shares each matrix product, and the large products run
  on Accelerate (`models/README.md`). `run` is thread-safe given its own state and scratch, and never allocates;
  each engine owns one scratch for its worker.
- `dsp/stems` — `StemSplitter` turns an input frame and its speech estimate into complex per-bin shares of the input
  for voice, ambience and reverb that always sum to one. `computeSensitivity` turns six nodes into per-bin weights;
  `computeTransfer` reweights the shares (renormalised, so they still sum to one) and applies the levels and the
  audition mode, giving one complex transfer per bin. The output frame is the input frame times the transfer, so the
  stems add up to the input by construction.
- `dsp/engine` — the 48 kHz engine. See below. A `Pipeline` is one quality and stereo mode: Linked runs one stream
  on the mid and applies its transfer to both channels; Stereo runs one per channel; Mid/Side runs one on the mid
  M = (L + R) / 2 and one on the side S = (L − R) / 2 and decodes L = M·T_M + S·T_S, R = M·T_M − S·T_S. Each
  stream's shares sum to one, so in every mode the stems add up to the input. A mono input is always Linked. The
  analysis shows the first channel; in Mid/Side its stems are decoded from both streams the same way.
- `dsp/resampler`, `dsp/rate_bridge` — streaming rational polyphase conversion, one sample at a time (no bursts), and
  the bridge that runs the engine at any host rate. Both directions use one prototype length K at the common rate R,
  with K − 1 a multiple of R / host rate, and the engine's latency is a multiple of 48000 / gcd(48000, host rate),
  so the whole round trip delays a whole number of host samples. That makes the residual `x_delayed − up(down(x))`
  line up exactly: added back with the ambience level, it restores what the round trip loses (above ~20 kHz), and
  with every level at unity the output is the delayed input at any rate.
- `PluginProcessor` — parameters (`params.hpp`; the IDs are frozen, `tests` checks them), state, bypass cross-fade,
  output gain, meters. The audition mode (`listen`) is not a parameter and is never saved.

## Stem outputs

Output buses 1–3 (Voice, Ambience, Reverb) are off by default, so the plug-in is a plain effect; each may be on
with the main output's channels, or stereo on a mono instance (the stem on both channels; AU enables every bus at
its default, stereo, and must still offer mono). When any is on, `prepare` asks the engine for stems: the pipeline also gives each
stream's stem transfers, `gain · weighted share`, decodes each stem to the channels like the mix (in Mid/Side from
the mid and the side), the stem spectra cross-fade with the mix's on a pipeline switch, and the worker synthesises
each into its own output ring. Their sum is the mix's transfer, so the stem outputs add up to the main output (to
float rounding, tested at 44.1–96 kHz and in Mid/Side). The `RateBridge` resamples each stem like the mix and adds
the round trip's residual to the ambience at its level. A sample the model was late for, or a frame it could not
separate, goes to the voice, as the mix plays it as voice (see the fallback below). The processor applies the output gain and the bypass
fade (to silence) to the stems; `listen` does not touch them. A stem the host left off is written to scratch.
JUCE's AU wrapper enables every bus, so AU always computes the stems: three more inverse FFTs per channel and hop.

## Threads and latency

The audio thread never waits in real time and never allocates (`tests` counts allocations). It writes its block into
an input ring indexed by absolute sample time (a non-finite sample as silence: it would stay in the model's running
averages for good), wakes the worker (an atomic counter's notify), and reads its output `latency` samples back from an
output ring. Where the worker has not produced a sample yet, the output plays the fallback instead, aligned, rather
than dropping out, and the sample is counted (Settings shows it).

The worker (`juce::Thread`, real-time options, joined to the host's audio workgroup; a high-priority ordinary thread
where real-time scheduling is not allowed) analyses every hop as soon as its
input exists, runs the model and overlap-adds. Frame `f` covers input `[(f − 1)·hop, (f + 1)·hop)`; processing frame
`k` finishes output up to `(k − 4)·hop`. So:

- algorithmic latency: frame `k` ends at sample `(k + 1)·hop − 1` and completes the output hop starting at
  `(k − 5)·hop`, so `kAlgorithmicLatency` = 960 + 4 × 480 − 1 = 2879 samples (60 ms), with each hop processed the
  moment its last sample exists (as offline);
- plus the worker's time. In real time the worker starts a frame once the block holding its last sample has been
  written, and with `latency = 2879 + D` that frame is first read `D` samples of input later, at the earliest
  `floor(D / block)` blocks later. So `D` is a whole number of blocks: one block, or as many as make up
  `kWorkerBudget` (one hop, 10 ms) when blocks are shorter. At 48 kHz that is 3391 samples (70.6 ms) at 512, 256,
  128 or 64-sample blocks, and 2879 + block above 480.

The worker's guarantee is the one the engine has always had: 10 ms or one block, whichever is longer, enough for High
in Stereo or Mid/Side mode while a quality change runs a second pipeline. The older `2880 + 480 + block` added a hop
on top of a block that already covered it. The rest of the latency is the model's (`models/README.md`).

Offline (`isNonRealtime`), the audio thread runs the pending hops itself under the worker's mutex, so a bounce never
contains unprocessed frames, never skips one, and does not depend on the block size (tested to 1e-6). A real-time
worker that keeps up gives exactly the offline output (tested).

### Late frames

**The fallback** is what a frame would give if it were all voice: the input at the voice's level in the mix (a voice
solo at unity, the other solos silent, Removed the rest), the voice stem at its level and the other stems silent. The
raw input is only transparent with every level at unity; with the ambience down it splices bursts of full noise into
the output. The alternatives were worse: holding the last frame's transfer needs an FFT, which the audio thread cannot
do when the worker itself has stalled, and it mutes a word that starts during the stall; a gain matched to the recent
output does the same to speech after a pause. Voice at its level is a scalar, so the audio thread and the worker
compute the same thing; it never drops the dialogue, and it is never louder than the voice level; with all three
levels equal it is exactly the processed output; and every sum holds through it (the stem outputs add up to the main
output, Removed is the input minus the mix). It is also what a frame the model could not separate already was. The
audio thread smooths its gains like the worker's levels.

**Into the fallback.** After each frame the worker also writes the frame's overlap-add tail (its second half, one hop
past `produced`) to a tail ring. A late sample `t` in `[produced, produced + hop)` plays `tail(t) + g·x(t)·w²(t −
produced)`, then `g·x(t)`: exactly the output if every later frame were the fallback, so the overlap-add's own window
cross-fades from the processed output into the fallback over a hop, with no step. **Out of it.** Once the output has
played past `produced`, a newer `produced` is not switched to: the output cross-fades from the continuation it was
playing to the new one over a hop, with the same `w²` curve (both exist for every sample, so the fade never waits).

**Skipping.** The audio thread reads no output before `heard = written − block − latency` any more, so frame `f`,
which completes output up to `(f − 4)·hop`, is inaudible once that is not later. A few inaudible frames are still
worth running, since they keep the model's state continuous (a 15 ms hiccup costs 15 ms of fallback). Once the next
five are inaudible, re-warming costs fewer model runs than catching up, so the worker skips to the first hop the audio
thread may still read: it analyses the frames that hop needs, overlap-adds the one before it as the fallback, and
resumes the model there. The model keeps its recurrent state (to it, a splice in the input: its running
normalisation is already adapted to the material, which a reset would throw away), but its next 4 estimates belong to
frames before the skip, so the output stays the fallback until it is warm again and then fades in over 8 hops, as on a
quality change. So after a stall of any length, the output is processed again about 13 hops (130 ms) after the worker
runs again (tested within 140 ms), rather than after it has worked through the backlog. Offline never skips.

**The ring invariant.** The audio thread may be writing input up to a block past `written`, so input before
`written + block − ring` may be being overwritten. A frame the worker runs reads input at most two hops before
`heard`, and the rings hold at least the latency, two blocks and four frames, so it never reads such input. The
worker also checks it before each frame, skipping a frame whose input is gone (offline, that can only follow a switch
from real time while the worker was far behind), and records the margin after reading; tests check that it stays
positive across stalls longer than the ring.

**Non-finite estimates.** Extreme but finite input can still overflow the model or the splitter. A pipeline whose
estimate or transfer is not finite is reset, and the output recovers from the fallback as after a skip.

Changing quality or stereo mode switches pipelines: the new one runs beside the old one until its estimates belong to
frames it has seen (4 hops), then the output spectra cross-fade over 8 hops (the outputs rather than the transfers,
since a left/right and a mid/side transfer do not apply to the same signals). A change waits while the engine
recovers from a skip, and a skip ends a switch in progress.

## Editor

`ui/editor` derives from `altvane::ui::EditorShell` (header, presets, A/B, undo, settings, scale and frame loop are
shared with Altvane EQ). `ui/visualiser` draws the engine's `AnalysisFrame`s (64 log bands, weighted by how much of each
bin a band covers, published through a `TriBuffer`) as stacked layers, and edits sensitivity nodes through
`ui::ParamEditor`. `ui/control_deck` holds the three stem cards; `ui/chrome` the footer and output rail;
`ui/theme/dialogue_tokens.hpp` the stem colours and sizes. Factory presets (`ui/factory_presets`) are installed once
into `Presets/Factory`.

## Verification

- `AltvaneDialogueTests` (Catch2): null tests at 44.1–192 kHz with irregular blocks, the real-time path while the model
  falls behind, late frames with the levels apart (a test hook runs the worker by hand, so stalls are deterministic):
  the fallback is the input at the voice's level, no step into or out of it, skipping after a stall and processing
  again within the bound, the ring invariant across stalls longer than the ring, a worker that keeps up matching the
  offline output, quality changes while playing, non-finite and overflowing input, impulse latency, the 48 kHz round trip's alignment, solo stems summing to the input, stem outputs
  summing to the main output and matching the solos at their levels (also late, bypassed, partly enabled and in
  Mid/Side), Mid/Side meters decoded from both streams, stereo-mode changes while playing, block-size independence,
  Removed being the input minus the mix, no allocation on the audio thread, frozen parameter IDs, state, text
  round-trips, editor accessibility and parameter mapping, factory presets; the network against values from ONNX
  Runtime, two streams run together against separate runs, the Accelerate path against the portable one, model
  loading under a decimal-comma locale, and the worker's latency budget.
- `AltvaneDialogueRender` for listening, CPU and real-time soak tests (`--realtime`), `AltvaneDialogueSnapshot` for
  visual review, `auval -v aufx AvDl Altv`, pluginval at strictness 10.
- `docs/BENCHMARK.md` records separation quality and CPU.
