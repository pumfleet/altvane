# Notice

## Altvane EQ

Altvane EQ (`products/eq`) is a fork of [ZL Equalizer 2](https://github.com/ZL-Audio/ZLEqualizer) by zsliu98 /
ZL Audio, used under the GNU Affero General Public License v3 (see `LICENSE.md`).

- The audio engine (`products/eq/source/dsp`, `source/zlp`, `source/chore`) is upstream code, vendored unmodified
  from the commit recorded in `products/eq/UPSTREAM`, so upstream fixes can be taken directly.
- The Altvane EQ editor (`products/eq/source/ui`) and the shared Altvane UI library (`libs/ui`) are new code, also
  licensed under AGPL-3.0.
- The original ZL Equalizer interface has been removed; Altvane EQ reads and writes the same preset and session
  format, so ZL Equalizer 2 presets and sessions load unchanged.

## Altvane Dialogue

Altvane Dialogue (`products/dialogue`) is new code, licensed under AGPL-3.0. It runs these components:

- the DPDFNet speech enhancement models by Ceva (Apache-2.0, `licenses/DPDFNet_LICENSE.txt`), embedded unmodified
  from the revision recorded in `products/dialogue/models/README.md`, and run by `source/dsp/dpdfnet.cpp`, which
  reimplements their streaming network after Ceva's reference code (`onnx_model/dpdfnet_48khz_hr.py`, Apache-2.0),
- r8brain-free-src by Aleksey Vaneev (MIT, `licenses/r8brain_LICENSE.txt`) and the PFFFT it bundles, by Julien
  Pommier (FFTPACK licence, `licenses/pffft_LICENSE.txt`).

## Altvane Compressor

Altvane Compressor (`products/compressor`) is a fork of [ZL Compressor](https://github.com/ZL-Audio/ZLCompressor) by
zsliu98 / ZL Audio, used under the GNU Affero General Public License v3 (see `LICENSE.md`).

- The audio engine (`products/compressor/source/dsp`, `source/zlp`, `source/chore`) is upstream code, vendored
  unmodified from the commit recorded in `products/compressor/UPSTREAM`, so upstream fixes can be taken directly.
- The processor (`products/compressor/source/PluginProcessor.*`) is adapted from upstream with routing fixes listed in
  `products/compressor/CHANGELOG.md`. The Altvane Compressor editor (`products/compressor/source/ui`) is new code,
  also licensed under AGPL-3.0.
- The original ZL Compressor interface has been removed; Altvane Compressor reads and writes the same preset and
  session format, so ZL Compressor presets and sessions load unchanged.

## Altvane Leveler

Altvane Leveler (`products/leveler`) is new code, licensed under AGPL-3.0. It uses no third-party code beyond what every
Altvane product shares.

## Altvane Limiter

Altvane Limiter (`products/limiter`) is a fork of [ZL Limiter](https://github.com/ZL-Audio/ZLLimiter) by zsliu98 /
ZL Audio, used under the GNU Affero General Public License v3 (see `LICENSE.md`).

- The audio engine (`products/limiter/source/dsp`, `source/zlp`, `source/chore`) is upstream code, vendored unmodified
  from the commit recorded in `products/limiter/UPSTREAM`, so upstream fixes can be taken directly.
- The processor (`products/limiter/source/PluginProcessor.*`) and state (`source/state`) are adapted from upstream.
  The Altvane Limiter editor (`products/limiter/source/ui`), output stage and loudness meter
  (`products/limiter/source/engine`) are new code, also licensed under AGPL-3.0.
- The original ZL Limiter interface has been removed; Altvane Limiter reads and writes the same preset and session
  format, so ZL Limiter presets and sessions load (with Altvane's extra parameters at their defaults).

## Third-party components

Third-party components and their licenses are listed in `licenses/`, including the Inter typeface (SIL OFL 1.1).
