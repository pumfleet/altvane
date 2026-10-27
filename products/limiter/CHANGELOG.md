# Changelog

## 0.1.0

First release of Altvane Limiter, based on the ZL Limiter engine (upstream commit `412c844`, a work in progress
upstream).

### Added on top of the engine

- A loudness meter in the audio engine (ITU-R BS.1770-4 / EBU R128: momentary, short-term, integrated, loudness range,
  maxima, true peak), so loudness is measured with the editor closed and in offline renders. It counts while the host
  plays and, with Auto on, starts over when playback starts.
- Loudness targets, shown on the display and the meters, and Match target.
- Dither to 24, 20 or 16 bits, flat or noise shaped, after the limiter.
- A DC filter before the limiter.
- Character: timing presets from gentle to loud.

### Compatibility

- ZL Limiter's parameters, preset format (`ZLLimiterPreset`) and state format: its presets and sessions load, with
  Altvane's extra parameters at their defaults.
- Installs alongside ZL Limiter (different plug-in codes and bundle identifier).

The upstream changelog is kept in [docs/UPSTREAM_CHANGELOG.md](docs/UPSTREAM_CHANGELOG.md) (upstream's file still
carries the ZL Spectrum Equalizer heading it was started from).
