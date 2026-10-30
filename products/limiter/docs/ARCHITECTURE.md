# Altvane Limiter architecture

Altvane Limiter is ZL Limiter's engine with a new editor, an output stage and a loudness meter. Paths below are
relative to `products/limiter/`. As with Altvane EQ and Altvane Compressor, **the engine is not modified**:
`source/dsp`, `source/zlp` and `source/chore` are vendored byte-identical from the upstream commit pinned in
[`UPSTREAM`](../UPSTREAM). `scripts/check_upstream.sh limiter` and CI enforce this;
`scripts/sync_upstream.sh limiter <ref>` moves the pin. ZL Limiter has no submodules of its own.

## What was adapted from upstream

These files are listed under `WATCH` in `UPSTREAM`, so a sync reports upstream changes to them for porting by hand.

- `source/PluginProcessor.*`:
  - creates the Altvane editor and holds the editor's `altvane::ui::Session` (undo, A/B, current preset);
  - adds Altvane's parameters (`source/engine/params.hpp`) after upstream's layout;
  - runs the output stage around the engine (below);
  - replaces upstream's value-measurement transport tracking, which fed its editor-side meters, with the loudness
    meter's: it counts while the host plays and starts over when playback starts;
  - applies oversampling changes itself, off the audio thread (below).
- `source/engine/engine_attach.hpp` stands in for upstream's `zlp::LimiterAttach` (`source/zlp/limiter_attach.*`,
  vendored but unused): it hands the engine every parameter but oversampling. `limiter_attach.cpp` is in `WATCH`, and
  a test fails if an engine parameter is missing from `EngineAttach`.
- `source/state/`:
  - `ui_state.*` and `property.*` are removed (they served upstream's GUI; the Altvane shell keeps its own settings).
  - `preset_json.*` moved here from upstream's preset browser, keeping the `"ZLLimiterPreset"` format, with a
    `toText()` for generating factory presets.
  - `state_definitions.hpp` keeps upstream's non-automatable state and adds Altvane's: loudness target, custom
    target values, loudness scale, auto reset and which panels are open. Upstream's UI-settings layout is dropped.

## Signal flow

```
input -> DC filter -> zlp::Controller (input gain, limiter, output gain / unity, bypass, delta) -> dither -> output
                                                                                                    \-> loudness meter
```

- The DC filter (`engine/dc_filter.hpp`, first-order high-pass at 5 Hz) and dither (`engine/dither.hpp`) run only
  while the limiter is heard: not in bypass, and dither not in delta. The DC filter fades between the input and the
  filtered signal over 50 ms when it is switched or bypassed, since the two differ by the offset and the low end's
  phase. Dither is TPDF, ±1 LSB, onto the 24, 20 or
  16-bit grid; Shaped feeds the error back through Wannamaker's 9-tap F-weighted filter, at 44.1 kHz and up.
- The loudness meter (`engine/loudness_meter.hpp`) runs on the audio thread and never allocates there: K-weighting
  (the engine's `dsp/loudness/k_weighting_filter.hpp`) into 10 ms slices; momentary and short-term from the last 40
  and 300 slices, with their maxima taken every slice, so a burst is not under-read for falling between updates;
  integrated loudness (400 ms blocks every 100 ms) and loudness range gated from histograms of 0.01 LU bins, so the
  gates are exact to 0.01 LU; true peak with the engine's 16× estimator. It publishes readings ten times a second
  through a `ui::TriBuffer`.
- The engine's lookahead is a fixed 5 ms delay inside the limiter, so changing it never changes the latency;
  oversampling does.

## Oversampling, off the audio thread

`zlp::Controller` switches oversampling inside `process()` when it sees the parameter change, and then calls
`triggerAsyncUpdate()` to report the new latency: it posts a message from the audio thread, which takes a lock and may
allocate. Altvane does not let the parameter reach the engine there:

- `EngineAttach` leaves oversampling out, so the controller only learns of it from `PluginProcessor`.
- A 20 Hz timer picks up any change (the editor, a session or preset, host automation) and hands the index to the
  controller on the message thread, re-preparing it as a host's `prepareToPlay` would, which reports the latency
  there and posts nothing. Not from a parameter listener: those run under the parameter's listener lock, which a
  host thread setting the same parameter would then wait on.
- While it does, the audio thread plays silence for any block that arrives rather than wait (an atomic flag; the
  audio thread never spins). The switch resets the engine, so a latency's worth of audio is lost with or without this.
- In an offline render (`isNonRealtime()`) the render thread switches at once, so the change lands on the block it was
  automated on; the engine then posts the latency change itself, which does not matter offline. The timer leaves the
  switch to it, and a render block waits for the engine rather than play silence, so a render never loses audio.

## Known engine issues (to raise upstream)

- The posting from the audio thread above. Worked around here; upstream could switch oversampling with a
  pre-allocated, lock-free path and report the latency from the message thread (e.g. polled by a timer).
- The engine's bypass (`PBypass`, and host bypass) switches between the limited and the delayed dry signal without a
  cross-fade, so it clicks while the limiter is reducing gain.
- Without oversampling, true peak mode lets the waveform through up to about 0.2 dB over the ceiling, as read by the
  tests' independent 16× interpolation (0.17 dB at 96 kHz on the golden programme; see docs/BENCHMARK.md). That is
  within a true-peak meter's tolerance (+0.2 dB in EBU Tech 3341) but worth knowing when the ceiling is tight; 2× and
  up stay under 0.07 dB.

## How the editor talks to the engine

- **The parameter trees:** `parameters_` (upstream IDs in `source/zlp/zlp_definitions.hpp`, Altvane's in
  `source/engine/params.hpp`) and `na_parameters_` (display and loudness settings, saved with the session but kept
  out of the undo history).
- **The loudness readings:** `PluginProcessor::pullLoudness()`, once per frame.
- **The engine's analyzer:** `zlp::Controller::getMagAnalyzerSender()` carries four 2-channel streams (input,
  gained input, limited, output) while `setAnalyzerEnabled(true)`, i.e. while the editor is showing.
  `ui/model/level_feed.*` reads it once per frame on the message thread into 10 ms columns (gained-input peak, output
  peak, gain reduction from the engine's `LimitReductionReceiver`, short-term loudness) and the meters' peaks
  (true peaks while the limiter works in true peak).

## The editor

`ui/editor.*` builds on the shared editor shell (`libs/ui`: header, presets, A/B, undo, settings, scaling).

- `history_view.*`: the scrolling display, drawn per pixel column with display ballistics (peaks fall at 36 dB/s,
  gain reduction recovers at 60 dB/s). Dragging the ceiling line edits the ceiling; dragging anywhere else edits the
  gain at the display's own scale (one pixel is one pixel of level).
- `meter_rail.*`: output (true) peak, gain reduction, momentary and short-term loudness with the integrated value and
  the target; LUFS, LU or K-system scales.
- `loudness_panel.*`: target, integrated against it, the R128 values, the short-term distribution, Match target.
- `control_card.*`: character, gain and ceiling, timing, unlink; `footer.*`: oversampling, dither, DC filter,
  audition, the display's range and length, loudness scale, latency, bypass.
- `model/character.hpp`: the timing presets. Each one is louder than the one before it at the same gain on a drum
  loop; a test checks the order.
- `factory_presets.*`: factory presets in ZL Limiter's format, and the preset codec, which keeps the session's display
  and loudness preferences when a preset loads (only the loudness target comes from the preset).
- Help balloons (`libs/ui/widgets/help_balloon.*`) explain each control; Settings can turn them off.

## Verification

- `AltvaneLimiterTests` (Catch2):
  - state and presets: the round trip, ZL Limiter's tags and preset format;
  - the limiter: latency, the ceiling (sample and true peak, checked by an independent 16× interpolation), unity gain,
    delta, bypass (host and parameter), the order of the characters;
  - the output stage: dither noise levels and grid, noise shaping, dither off in bypass and delta, the DC filter and
    its fades (no step with an offset or heavy low end, switched or bypassed);
  - loudness: EBU Tech 3341 cases 1–5 and 9–23 (steady tones, gating, momentary and short-term values and maxima,
    true peak) and Tech 3342 cases 1–4 (loudness range), at 44.1, 48 and 96 kHz, synthesised from the specifications'
    descriptions; block-size independence, the transport reset;
  - golden behaviour of the vendored engine (`golden_tests.cpp`), since CI only checks its bytes: the ceiling (sample
    and true peak) at every oversampling factor at 44.1 and 96 kHz, a gain-reduction fingerprint of a drum loop,
    recovery time per character, and channel delta. A sync that changes the sound fails these; update the numbers
    only for a change that is wanted;
  - the audio thread does not allocate, including across oversampling changes; oversampling switches on the message
    thread (offline: on the render thread); every engine parameter but oversampling is attached;
  - the editor: accessibility and undo of the controls, characters, the display's gain nudge, Match target, factory
    presets, panels following the session.
- `AltvaneLimiterSnapshot` renders the editor through the real processor for visual review.
- pluginval at strictness 10 in CI and before releases.
