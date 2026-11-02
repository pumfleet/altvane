# Altvane Compressor architecture

Altvane Compressor is ZL Compressor's engine with a new editor. Paths below are relative to `products/compressor/`.
As with Altvane EQ, **the engine is not modified**: `source/dsp`, `source/zlp` and `source/chore` (and the
`zldsp_fft` submodule) are vendored byte-identical from the upstream commit pinned in [`UPSTREAM`](../UPSTREAM).
`scripts/check_upstream.sh compressor` and CI enforce this; `scripts/sync_upstream.sh compressor <ref>` moves the pin.

## What was adapted from upstream

These files are listed under `WATCH` in `UPSTREAM`, so a sync reports upstream changes to them for porting by hand.

- `source/PluginProcessor.*`:
  - creates the Altvane editor;
  - drops upstream's global UI-settings tree (`state_`, `property_`), which only its interface used;
  - holds the undo manager, the A/B slots and the current preset, so they survive closing the editor;
  - fixes routing bugs (see the [changelog](../CHANGELOG.md));
  - processes host blocks in pieces of at most the prepared size, since the engine's buffers hold only that much;
  - keeps the main input in a small ring buffer (20 ms of lookahead, the oversamplers' filters and a block) and, in
    host bypass, plays it delayed by the reported latency instead of whatever the engine, delta, the side-chain listen
    or a solo would play. The latency reaches the audio thread as an atomic copy, set when the processor hears its
    own latency change (it listens to itself as an `AudioProcessorListener`).
- `source/state/`:
  - `ui_state.*` is removed (it depended on upstream's GUI).
  - `preset_json.*` moved here from upstream's preset browser, keeping the `"ZLCompressorPreset"` format, with a
    `toText()` for generating factory presets.
  - `property.hpp` stores under the Altvane manufacturer folder.
  - Everything else is identical to upstream.

## How the editor talks to the engine

The editor reads and writes only these:

- **The parameter tree** (`PluginProcessor::parameters_`). The IDs are in `source/zlp/zlp_definitions.hpp`, e.g.
  `threshold`, `ratio`, `knee_width`, `side gain` (with a space), `freq0`…`freq7`.
- **The non-automatable display tree** (`na_parameters_`, `source/state/state_definitions.hpp`): display ranges,
  analyzer settings and which panels are shown. It is saved with the session.
- **`zlp::CompressController`:**
  - `getMagAnalyzerSender()` carries 4 streams × 2 channels: input, compressed (before makeup), output, and side
    chain (after its EQ).
  - It also provides the LUFS matcher behind makeup Learn, and editor on/off.
- **`zlp::EqualizeController`:** `getFFTAnalyzerSender()` carries the side-chain spectrum after the EQ, and is fed
  only while `setFFTAnalyzerON(true)`. `setSoloBand()` plays the side chain through the band's solo filter instead of
  the output; the side-chain EQ view sets it, and turns it off when the band goes, the drawer closes or the editor
  closes. Solo is not saved.

The engine has no gain-reduction output. The editor measures gain reduction per display column as
`compressed − input`, as upstream's interface did. It draws the transfer curve with its own instances of the
engine's gain computers (`source/dsp/compressor/computer`), so the curve is the engine's maths.

## The editor

The editor is specified by the approved interactive mock, [docs/design/mock.html](design/mock.html). It is built on
the shared editor shell in `libs/ui` (header, presets, A/B, undo, settings, interface scale), and everything below
runs on the message thread, once per frame:

- `ui/model/comp_bridge.*`: parameter access. Edits go through the shell's `ParamEditor`, so each gesture is one
  host gesture and one undo step. Display settings (time, scale, peak / RMS, which panels are open) live in the
  non-automatable tree.
- `ui/analysis/level_history.*`: drains the magnitude analyzer into one column per pixel of the level display, plus
  the meter peaks and the detector level.
- `ui/analysis/transfer_model.*`: the static curve, from the engine's own gain computers and the engine's rules for
  direction, range, wet and makeup (`CompressController::appleSideBuffer`). `editor_tests.cpp` checks it against the
  processor's measured output in all four directions.
- `ui/analysis/side_spectrum.*`: the side-chain spectrum after its EQ, at one FFT resolution.
- `ui/graph/`: the level display, the knee display and the side-chain EQ view. The EQ view has a selected band,
  whose shape, frequency, gain, Q or slope and Solo sit along its top as shared widgets (typed entry, keyboard and
  screen-reader values for free). The view itself takes keyboard focus: Return adds a band, 1 to 8 select one, the
  arrows move it (one undo step per burst, like the wheel), B bypasses, Delete removes, holding S solos; its
  accessibility value reads out the selected band.
- `ui/panels/`: the control card, the advanced popover, the meter rail, the side-chain drawer and the footer.
  Controls explain themselves through the shell's help balloons (`ui::setHelp`), which the HELP switch in settings
  turns off, as in the Limiter.
- `ui/factory_presets.*`: the eight factory presets of the mock, installed by the shell's preset manager, and the
  preset codec: presets are ZL Compressor preset files, and reading one swaps its display tree for the session's, so
  loading a preset never changes the view. Host sessions still restore the display.

`AltvaneCompressorSnapshot <dir> [scale]` renders the editor's states headlessly, with the processor running on a
synthetic drum loop; its `00_screenshot.png` at scale 2 is `docs/screenshot.png`.

## Verification

- `AltvaneCompressorTests` (Catch2) covers:
  - the knee display's curve against the processor's measured output, for every direction;
  - the level history's input, output and gain-reduction readings;
  - every control mapping to its host parameter and carrying help, and the editor running with audio flowing;
  - every parameter surviving a state round trip, the parameter IDs (pinned), and ZL Compressor's state tags;
  - a ZL Compressor 0.5.0 session (legacy tags) and a 0.6.1 preset loading, from `tests/fixtures` (how they were
    made is in `compat_tests.cpp`);
  - factory presets loading, keeping the session's display;
  - a steady tone settling on the compression curve, range and makeup, and Learn;
  - host bypass passing the input delayed by the latency, in every layout, both precisions, and with lookahead,
    oversampling, delta, listen or solo on;
  - oversized host blocks, output independent of the block size, and no allocation on the audio thread;
  - latency for lookahead (positive and negative) and oversampling;
  - side-chain listen on a mono track with a stereo side chain;
  - the side-chain EQ from the keyboard, typed values, and band solo.
- auval and pluginval (strictness 10) in CI and before releases.
