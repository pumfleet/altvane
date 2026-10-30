# Changelog

## Unreleased

### Fixed

- The DC filter fades in and out over 50 ms when it is switched on or off and when the limiter is bypassed. It
  switched hard before, which stepped by the DC offset (or by the phase difference in heavy low end).
- Changing oversampling no longer posts a message from the audio thread: the switch (and the latency report) happens
  on the message thread, or on the render thread in offline renders. See docs/ARCHITECTURE.md.
- The loudness meter's maximum momentary and short-term loudness are measured every 10 ms instead of every 100 ms. A
  burst that fell between updates read up to 0.45 LU low (EBU Tech 3341 cases 13 and 14); the momentary and
  short-term readouts still update ten times a second.

### Tests

- Golden-behaviour tests for the vendored engine (ceiling at every oversampling factor, a gain-reduction fingerprint,
  recovery per character, channel delta), so an engine sync that changes the sound fails CI.
- The loudness meter against EBU Tech 3341 cases 1–5 and 9–23 and Tech 3342 cases 1–4, at 44.1, 48 and 96 kHz.

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
