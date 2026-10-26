# Notice

Contour is a fork of [ZL Equalizer 2](https://github.com/ZL-Audio/ZLEqualizer) by zsliu98 / ZL Audio,
used under the GNU Affero General Public License v3 (see `LICENSE.md`).

- The audio engine (`source/dsp`, `source/zlp`, `source/chore`) is upstream code and is kept unmodified,
  so upstream fixes can be merged directly.
- The Contour editor (`source/ui`) is new code, also licensed under AGPL-3.0.
- The original ZL Equalizer interface has been removed; Contour reads and writes the same preset and
  session format, so ZL Equalizer 2 presets and sessions load unchanged.

Third-party components and their licenses are listed in `licenses/`, including the Inter typeface (SIL OFL 1.1).
