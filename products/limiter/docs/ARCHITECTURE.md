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
    meter's: it counts while the host plays and starts over when playback starts.
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
  while the limiter is heard: not in bypass, and dither not in delta. Dither is TPDF, ±1 LSB, onto the 24, 20 or
  16-bit grid; Shaped feeds the error back through Wannamaker's 9-tap F-weighted filter, at 44.1 kHz and up.
- The loudness meter (`engine/loudness_meter.hpp`) runs on the audio thread and never allocates there: K-weighting
  (the engine's `dsp/loudness/k_weighting_filter.hpp`) into 100 ms steps; momentary and short-term from the last 4 and
  30 steps; integrated loudness and loudness range gated from histograms of 0.01 LU bins, so the gates are exact to
  0.01 LU; true peak with the engine's 16× estimator. It publishes readings ten times a second through a
  `ui::TriBuffer`.
- The engine's lookahead is a fixed 5 ms delay inside the limiter, so changing it never changes the latency;
  oversampling does.

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
  - the output stage: dither noise levels and grid, noise shaping, dither off in bypass and delta, the DC filter;
  - loudness: EBU Tech 3341 (steady tones, gating) and Tech 3342 (loudness range) cases, block-size independence,
    maxima and true peak, the transport reset;
  - the audio thread does not allocate;
  - the editor: accessibility and undo of the controls, characters, the display's gain nudge, Match target, factory
    presets, panels following the session.
- `AltvaneLimiterSnapshot` renders the editor through the real processor for visual review.
- pluginval at strictness 10 in CI and before releases.
