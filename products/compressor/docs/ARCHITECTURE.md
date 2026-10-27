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
  - fixes three routing bugs (see the [changelog](../CHANGELOG.md)).
- `source/state/`:
  - `ui_state.*` is removed (it depended on upstream's GUI).
  - `preset_json.*` moved here from upstream's preset browser, keeping the `"ZLCompressorPreset"` format.
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
  only while `setFFTAnalyzerON(true)`. It also handles band solo.

The engine has no gain-reduction output. The editor measures gain reduction per display column as
`compressed − input`, as upstream's interface did. It draws the transfer curve with its own instances of the
engine's gain computers (`source/dsp/compressor/computer`), so the curve is the engine's maths.

## The editor

The designed editor is specified by the approved interactive mock, [docs/design/mock.html](design/mock.html). Open
it in a browser to see it; its curve maths is ported from the engine.

It will be built on the editor shell, header, presets, A/B, settings and meters shared with the other Altvane
products in `libs/ui`. Until then, `source/ui/editor.*` is a placeholder: the Altvane header over JUCE's generic
parameter list.

## Verification

- `AltvaneCompressorTests` (Catch2) covers:
  - the state round trip and ZL Compressor's state tags;
  - a steady tone settling on the compression curve;
  - range and makeup;
  - host bypass;
  - latency for lookahead (positive and negative) and oversampling;
  - side-chain listen on a mono track with a stereo side chain.
- auval and pluginval (strictness 10) in CI and before releases.
