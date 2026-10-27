# Altvane De-Esser architecture

Paths are relative to `products/deesser/`. Everything here is Altvane code except PFFFT (r8brain's copy, shared by the
monorepo).

## Signal path

```
side chain (the track or the external input), per detection channel:
  ─► detection band: HP4 at band_low [─► LP4 at band_high] ─► peak envelope (0.3 / 30 ms) ─► band level (dB)
                                     └─► (Vocal) band vs body below band_low, 5 / 120 ms each ─► confidence c
  ─► wanted reduction = min(range, knee6(band − threshold) · c) ─► stereo link
  ─► look-ahead window maximum ─► attack (spans the look-ahead) / release (40–120 ms, by ess length) ─► GR

audio: ─► delay (look-ahead) ─► [mid/side encode] ─► y = x + (g − 1)·part ─► [decode] ─► listen ─► output ─► bypass
         part: x (Wide) | high shelf (Split, Shelf) | unity-peak band-pass (Split, Bell)
```

- `dsp/filters.hpp` — the topology-preserving state-variable filter (Simper), its coefficients (from a frequency, or
  from the prewarped g directly, cheap enough per sample), `Ramp` (a linear ramp that lands on its target exactly)
  and conversions.
- `dsp/detector.hpp` — one detection channel. The band's fast envelope is what the threshold sees (and what the
  history draws). In Vocal mode the band and the body below it are compared with the same, slower ballistics, so a
  sharp onset (a click, a plosive's burst) is not mistaken for an ess just because a fast envelope got there first.
  Confidence is a smoothstep from band − body = −12 dB (none) to 0 dB (all).
- `dsp/gain_computer.hpp` — the knee and range, the look-ahead window's running maximum (a monotonic queue in a ring
  sized in `prepare`, so nothing allocates) and the program-dependent smoothing. The reduction settles to exactly 0
  below 10⁻⁴ dB, which is what makes an idle de-esser exact.
- `dsp/engine` — channels, linking, mid/side, the delay lines, the reduction and everything the editor reads.
  - **Wide**: `y = g·x`.
  - **Shelf**: `y = lp + k√g·bp + g·hp` of one SVF, a second-order high shelf down to `g`, written as
    `x + (g − 1)·hp + (√g − 1)·k·bp`. Its g is the band's low edge times `g^(1/4)` (recomputed per sample), which
    puts the poles at `f·g^(1/4)` and the zeros at `f·g^(−1/4)`: the shelf's midpoint stays on the band's edge
    however deep it goes.
  - **Bell**: `y = x + (g − 1)·k·bp`, the classic peaking cut, centred on the band (geometrically) with its −3 dB
    edges at the band's; an open band's bell stops at 16 kHz.
  - The three weights, the listen mix, bypass and output are linear `Ramp`s (20, 15, 50 and 30 ms) that land exactly,
    so a settled de-esser with nothing to reduce is bit-exact in stereo (mid/side costs one rounding step).
  - Controls are read on a fixed 32-sample grid (band edges smoothed in log frequency there), so the output never
    depends on the host's block size.
  - A look-ahead change fades between the old and new delay taps over 10 ms, one change at a time.
- `dsp/learner.hpp` — `ThresholdLearner`: the median of the esses' peak levels, less 4 dB.
- `dsp/feeds.hpp` — lock-free single-producer/single-consumer rings the audio thread writes (only while the editor is
  showing) and the editor drains: history frames (240 per second: input and output peaks, the band's envelope, the
  largest reduction, the confidence), analysis samples (the detection signal, the output, whether it was an ess) and
  ess events (the peak level of each, for Learn). When the editor falls behind, new items are dropped.
- `PluginProcessor` — parameters (`params.hpp`; the IDs are frozen, `tests` checks them), state, the side-chain bus,
  peaks for the meters, latency. The listen mode is not a parameter and is never saved.

## Threads and latency

The audio thread never waits and never allocates (`tests` counts allocations, including across look-ahead and listen
changes). The latency is the look-ahead, reported in `prepareToPlay` and whenever the parameter changes. Hosts are only told
on the message thread: a change made there is reported at once, and one automated from the audio thread is picked up
by a 10 Hz timer (posting a message from the audio thread could lock or allocate). Bypass goes through the same
delay, so it keeps the latency. A change of stereo mode resets both channels' detection and reduction, so a channel
that sat idle, or now carries mid instead of left, never resumes with stale reduction.

## Editor

`ui/editor` derives from `ui::EditorShell` (header, presets, A/B, undo, settings, scaling, frame loop) and lays out:

- `ui/history_view` — the level history. Frames are grouped on a fixed grid (by absolute frame index), so the
  picture does not shimmer as it scrolls. Layers, back to front: the full signal (graphite), the detection band after
  reduction (steel), what was taken (the band before minus after, in the sibilance colour), ess ticks along the floor,
  the threshold, gain reduction from the top on its own scale (0–24 dB over the top 42%).
- `ui/band_view` — the spectrum (pre, post and the esses' average), the detection band and its edges, the live gain
  curve (the analog prototypes of the engine's shelf and bell, through a spring), the range's curve dashed behind it,
  and the finder's pin.
- `ui/analyzer` — 4096-point FFTs (PFFFT) with 50 % overlap over the drained analysis samples, on the message thread
  once a frame; 240 log-spaced points from 200 Hz to 22 kHz. Frames that are mostly ess feed a running average whose
  peak between 2 and 16 kHz (smoothed over a third of an octave) is the finder's estimate; it shows after 0.15 s of
  esses and is sure after 0.5 s.
- `ui/control_deck` — Threshold and Range (hero knobs), Learn (with a progress ring around the threshold), and
  segmented rows for mode, band, shape and listen. Choosing a shape while in Wide switches to Split in the same undo
  step.
- `ui/chrome` — the footer (stereo mode and link, side chain, look-ahead, output, bypass) and the meter rail (gain
  reduction from the top with a two-second hold, and the output).
- `ui/theme/deesser_tokens.hpp` — the sibilance colour (apricot, kept apart from the accent choices and the
  reduction's red), sizes and the window.

Every edit goes through `ui::ParamEditor`: one host gesture and one undo step per drag, including the two-edge band
drag, the wheel on the band (ended 450 ms after the last notch), Learn and the finder's fit. Controls end only the
edit they opened (`ParamEditor::getEditId`), so a wheel's timer never ends a drag another control began since, and
Learn waits for an edit in progress to finish before setting the threshold.

## Verification

- `tests/engine_tests.cpp` — exact null at rest in every mode at 44.1/48/96 kHz, latency and its changes, reduction
  to the threshold and within the range, split vs wide, bell vs shelf, Vocal vs Allround, delta + output = input,
  trigger band-limiting, link, mid only, external side chain, block-size independence, no allocations, bypass,
  Learn's events, frozen IDs, state and text round trips.
- `tests/analysis_tests.cpp` — the finder lands within a sixth of an octave of the esses (4.5, 7 and 9.5 kHz at 44.1
  and 96 kHz); the spectrum reads a tone at its level and frequency.
- `tests/editor_tests.cpp` — accessibility and text entry, one undo step per threshold drag, band drag, finder fit
  and Learn, host parameter menus, listen and learn never saved, factory presets complete.
- `AltvaneDeEsserSnapshot` renders the editor's states for review; `AltvaneDeEsserRender` renders audio and measures
  CPU.
