# Altvane Compressor

A compressor, upward compressor and expander built on the audio engine of
[ZL Compressor](https://github.com/ZL-Audio/ZLCompressor).

**Status: in development.** The engine, editor, build, tests and packaging are in place. The editor follows the
approved mock in [docs/design/mock.html](docs/design/mock.html):

- a scrolling level display (input, output and gain change) with a threshold line you can drag;
- a knee display with the engine's exact transfer curve, lit up to the detector level, with handles for threshold,
  ratio and knee width;
- a floating control card, with the detection, envelope and clipper controls behind More;
- input, gain-change and output meters;
- a side-chain drawer with its own 8-band EQ over the spectrum the detector hears.

- **Four directions:** compress, inflate (upward compression), expand and shape (upward expansion), with a soft knee,
  a curve control above the threshold and a floor for expansion.
- **Four styles:** Clean (feed-forward), Classic (feedback), Optical and Vocal.
- **Peak and RMS detection**, blended, with their own speed. Envelope controls are attack, release, hold, pump and
  smooth. Range limits the gain change, and makeup gain can be learned from loudness.
- **Side chain:** internal or external, mid/side or left/right with link and per-channel amounts, and an 8-band EQ
  with band solo.
- **Lookahead** of up to 20 ms, or negative to delay the side chain instead. **Oversampling** goes up to 8×, plus a
  soft clipper, wet mix and a delta listen.

## Build from source

Altvane Compressor is built from the [Altvane monorepo](../../README.md); see its README for requirements and options.

```console
cmake --preset mac-arm64-release -DALTVANE_PRODUCTS=compressor
cmake --build --preset mac-arm64-release
```

Compressor-specific options:

- `ALTVANE_COMPRESSOR_MAX_OVERSAMPLE`: highest oversampling rate as a power of two, 3 (8×) by default. Builds with
  different values are not session-compatible.

With `ALTVANE_BUILD_TESTS=ON` the build also produces `AltvaneCompressorTests`:

```console
build/mac-arm64-debug/products/compressor/AltvaneCompressorTests
```

Releases are built by the `Release` workflow from `compressor-vX.Y.Z` tags.

See [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) for how the product is put together.

## Relationship to ZL Compressor

Altvane Compressor is a fork. The audio engine (`source/dsp`, `source/zlp`, `source/chore`) is ZL Compressor's,
vendored unmodified from the upstream commit pinned in [UPSTREAM](UPSTREAM), which CI checks;
`scripts/sync_upstream.sh compressor` brings in newer upstream engine code. The interface in `source/ui` is new.
Altvane Compressor uses ZL Compressor's parameters, preset format and session format, and installs alongside it.

## AI transparency

Altvane Compressor's interface was developed with the help of an AI coding assistant (Claude). Its output is reviewed
by the maintainer, who takes responsibility for the quality, safety and licensing of the result.

## License

Altvane Compressor is licensed under the GNU Affero General Public License v3; see [LICENSE.md](../../LICENSE.md) and
[NOTICE.md](../../NOTICE.md).

- Audio engine from [ZL Compressor](https://github.com/ZL-Audio/ZLCompressor), copyright (c) 2025-2026
  [zsliu98](https://github.com/zsliu98)
- [JUCE](https://github.com/juce-framework/JUCE), template from [pamplejuce](https://github.com/sudara/pamplejuce)
- [Highway](https://github.com/google/highway) by Google
- [Inter](https://github.com/rsms/inter) by The Inter Project Authors (SIL Open Font License)
