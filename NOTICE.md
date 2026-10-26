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

## Third-party components

Third-party components and their licenses are listed in `licenses/`, including the Inter typeface (SIL OFL 1.1).
