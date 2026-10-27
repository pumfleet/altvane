# Altvane Dialogue architecture

Paths are relative to `products/dialogue/`. Everything here is Altvane code except the models (`models/README.md`),
ONNX Runtime and PFFFT.

## Signal path

```
host rate ─► RateBridge: down to 48 kHz ─► Engine ─► up to the host rate ─► + g·(x_delayed − round trip)
                                            │
             audio thread: copy into the input ring, copy out of the output ring (or the delayed input if late)
                                            │
             worker thread, per 10 ms hop:  STFT (960 Vorbis, 480 hop) ─► model ─► stem shares ─► transfer ─► iSTFT
```

- `dsp/spectral` — the STFT constants, the Vorbis window and a 960-point real FFT (PFFFT: 960 = 2⁶·3·5).
- `dsp/separator` — `SeparatorModel`, one ONNX Runtime session per model shared by every instance (`Run` is
  thread-safe), and `SeparatorStream`, one stream's recurrent state in preallocated, ping-ponged buffers. The model
  returns the speech estimate of the frame `kModelLag` (4) hops before the one it was given.
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

## Threads and latency

The audio thread never waits in real time and never allocates (`tests` counts allocations). It writes its block into
an input ring indexed by absolute sample time, wakes the worker (an atomic counter's notify), and reads its output
`latency` samples back from an output ring. If the worker has not produced a sample yet, it reads the input ring
instead, so a late frame plays unprocessed, aligned, rather than dropping out, and is counted (Settings shows it).

The worker (`juce::Thread`, real-time options, joined to the host's audio workgroup) analyses every hop as soon as its
input exists, runs the model and overlap-adds. Frame `f` covers input `[(f − 1)·hop, (f + 1)·hop)`; processing frame
`k` finishes output up to `(k − 4)·hop`. So:

- algorithmic latency: 960 + 4 × 480 = 2880 samples (60 ms),
- plus one hop for the worker to compute in, plus one block for the input to arrive in:
  `latency = 2880 + 480 + max block` at 48 kHz (3872 = 80.7 ms at 512).

Offline (`isNonRealtime`), the audio thread runs the pending hops itself under the worker's mutex, so a bounce never
contains unprocessed frames and does not depend on the block size (tested to 1e-6).

Changing quality or stereo mode switches pipelines: the new one runs beside the old one until its estimates belong to
frames it has seen (4 hops), then the output spectra cross-fade over 8 hops (the outputs rather than the transfers,
since a left/right and a mid/side transfer do not apply to the same signals).

## Editor

`ui/editor` derives from `altvane::ui::EditorShell` (header, presets, A/B, undo, settings, scale and frame loop are
shared with Altvane EQ). `ui/visualiser` draws the engine's `AnalysisFrame`s (64 log bands, weighted by how much of each
bin a band covers, published through a `TriBuffer`) as stacked layers, and edits sensitivity nodes through
`ui::ParamEditor`. `ui/control_deck` holds the three stem cards; `ui/chrome` the footer and output rail;
`ui/theme/dialogue_tokens.hpp` the stem colours and sizes. Factory presets (`ui/factory_presets`) are installed once
into `Presets/Factory`.

## Verification

- `AltvaneDialogueTests` (Catch2): null tests at 44.1–192 kHz with irregular blocks, the real-time path while the model
  falls behind, impulse latency, the 48 kHz round trip's alignment, solo stems summing to the input (also in
  Mid/Side), Mid/Side meters decoded from both streams, stereo-mode changes while playing, block-size independence,
  Removed being the input minus the mix, no allocation on the audio thread, frozen parameter IDs, state, text
  round-trips, editor accessibility and parameter mapping, factory presets.
- `AltvaneDialogueRender` for listening, CPU and real-time soak tests (`--realtime`), `AltvaneDialogueSnapshot` for
  visual review, `auval -v aufx AvDl Altv`, pluginval at strictness 10.
- `docs/BENCHMARK.md` records separation quality and CPU.
