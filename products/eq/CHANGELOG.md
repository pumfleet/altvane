# Changelog

## Unreleased

### Changed

- Sessions and presets record the layout of the state they were saved in (`schema_version="1"` on the state's root),
  so a later version can upgrade them. A state without it, saved before, loads as it always has; one saved by a
  newer version loads every setting this version has and leaves the rest.

### Fixed

- Restoring a session or preset puts each switch exactly where it was saved. One a host had left between on and off
  (0.4 is off) went on reporting that value to the host.

## 0.1.0

First release of Altvane EQ (developed as "Contour"), based on the ZL Equalizer 2.x engine (forked at 1.4.1).

### Interface

- New editor: full-bleed frequency graph, floating band panel, minimal header and footer
- Response curves computed from the engine's filter coefficients in every processing mode, including complex sums
  for the parallel structure, and within 0.01 dB of the processed audio in the zero-latency modes (bands above
  Nyquist and steep low-frequency bands at high sample rates included); curves glide on undo, preset changes and
  typed values
- Direct manipulation: node drag with fine and axis lock, Q handles, dynamic range handle, lasso and group moves,
  scroll for Q and gain, keyboard nudging and band cycling
- Dynamic bands show their static, target and live responses; the side-chain detector is edited on the graph
- Spectrum Grab, piano roll, collision display, freeze on hover
- EQ Match with side-chain, flat, natural, balance or file references, drawable correction, progressive fitting
- Preset browser with folders, favourites and search; A/B compare; undo/redo that survives closing the editor
- Output options: static gain compensation, phase invert, output channel, lookahead, loudness match
- Settings: interface size, accent colour, reduce motion, drag and scroll speed
- Keyboard focus and screen-reader support for every control; host parameter menus on right-click

### Engine integration

- Host blocks larger than the size announced in `prepareToPlay` are processed in pieces. ZL Equalizer 2 copied them
  past the end of its buffers.
- The processor no longer keeps pointers into the host's audio buffers between blocks (double precision).

### Compatibility

- Same parameters and state format as ZL Equalizer 2: presets and sessions load unchanged
- Installs alongside ZL Equalizer 2 (different plug-in codes and bundle identifier)

The upstream changelog is kept in [docs/UPSTREAM_CHANGELOG.md](docs/UPSTREAM_CHANGELOG.md).
