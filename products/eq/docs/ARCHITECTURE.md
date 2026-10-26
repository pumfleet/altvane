# Contour architecture

Contour is ZL Equalizer's engine with a new editor. The rule that shapes everything else: **the engine is not
modified.** `source/dsp`, `source/zlp` and `source/chore` stay byte-identical to upstream (CI enforces this), so
upstream fixes merge cleanly. The editor lives in `source/ui` and talks to the engine only through:

- the parameter tree (`PluginProcessor::parameters_`, IDs like `freq3`, see `source/zlp/zlp_definitions.hpp`),
- the non-automatable session tree (`parameters_NA_`: analyzer and display settings),
- a handful of `zlp::Controller` calls (editor on/off, analyzer FIFO, live dynamic gain, solo, loudness match,
  learned threshold, match bypass).

The processor gained only editor-facing members: the undo manager, A/B slots and the current preset path.

## Layout of `source/ui`

| Folder | Contents |
|---|---|
| `model/` | `EngineBridge` (typed parameter access, gestures, undo), `Selection`, band / session / preset operations, user settings |
| `analysis/` | `ResponseModel` (curves), `SpectrumWorker` (analyzer thread), `MatchEngine` (EQ Match) |
| `graph/` | `GraphView`: everything drawn on and done with the graph |
| `panels/` | band panel, header / footer / output rail, sheets (presets, settings), match panel |
| `widgets/` | knob, value field, selector, buttons, chips, switch, icons |
| `theme/` | design tokens, fonts, look-and-feel |
| `util/` | frequency / gain scales, value formatting, springs, lock-free triple buffer |

## Threads

- **Message thread**: all components and all parameter writes. `EngineBridge` listeners (which may fire on the audio
  thread) only raise atomic flags; `refresh()` copies values into snapshots once per frame, and changed flags
  accumulate until `clearChanges()` at the end of the frame.
- **Spectrum worker** (`SpectrumWorker`): woken every frame; drains the engine's analyzer FIFO, runs the upstream
  multi-resolution FFT chain, builds spectrum paths, peaks for Spectrum Grab and the collision gradient, and feeds
  `MatchEngine` from the same samples. Results cross threads through `TriBuffer`.
- **Match fitting thread** (`MatchEngine`): runs the upstream optimizer one band at a time and posts each result to
  the message thread.

Frames are driven by a `juce::VBlankAttachment`; the editor ticks the bridge, graph, panels and sheets, then wakes the
spectrum worker.

## Curves

`ResponseModel` evaluates each band on the message thread (a band costs well under 0.1 ms), so curves are
frame-locked with the nodes:

- minimum phase and SVF run digital biquads: `FilterDesign::updateCoeffs<IvantsovCoeff>` evaluated on the unit
  circle,
- the parallel structure sums low-order peaks and shelves as complex branches, `y = x + Σ (g − 1)·branch(x)`,
  followed by the series bands,
- matched, mixed and zero phase are FIR-corrected towards the analog prototype, so `zldsp::filter::Ideal` is used.

`tests/model_tests.cpp` checks drawn curves against the processor's measured impulse response in every structure.

## Editing and undo

Every user gesture is `beginEdit → setPlain… → endEdit` on `EngineBridge`: the host sees one gesture per parameter and
the undo history gets one transaction. One-shot changes use `setNow`. `cancelEdit` restores without an undo step
(used by EQ Match's Cancel). The undo manager lives in the processor so history survives closing the editor.

## Verification

- `Tests` (Catch2): formatting and scales, band operations and undo, copy/paste, A/B, curve accuracy, EQ Match end
  to end, accessibility contracts.
- `ContourSnapshot`: renders the editor headlessly with the real processor running on synthetic audio and writes
  PNGs of the main states, for visual review.
- pluginval (strictness 10) and auval in CI and before releases.
