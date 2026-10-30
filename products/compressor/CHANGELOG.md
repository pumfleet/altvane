# Changelog

## Unreleased

First version of Altvane Compressor, based on the ZL Compressor engine (forked at 0.6.1, upstream commit `2f7b45c`).

### Editor

- A new editor, following the approved design: scrolling level display with a draggable threshold, knee display with
  direct handles, floating control card, IN / GR / OUT meters, and a side-chain drawer with an 8-band EQ over the
  side-chain spectrum.
- Side-chain EQ band solo (the Solo switch, the band menu, or holding S), and the selected band's shape and values
  along the top of the EQ, where they can be typed. The EQ works from the keyboard and reads out its selected band to
  screen readers.
- Eight factory presets: Drum Smash, Drum Bus Glue, Vocal Leveler, Mix Bus Gentle, Bass Control, Parallel Smash,
  Upward Inflate and Gate-ish Expander.
- Loading a preset keeps the display (time, scale, open panels) as it is. ZL Compressor saves its display in presets.
- Help balloons on hover, which Settings can turn off.
- Presets, A/B, undo and settings shared with the other Altvane products.

### Engine integration

- Mono tracks with a stereo side chain now hear the mid of the side chain when listening to it. ZL Compressor output
  silence here unless a side-chain EQ band was soloed.
- Host bypass is honoured on mono tracks when the host processes in double precision.
- Host bypass plays the input delayed by the reported latency, in every channel layout and both precisions. ZL
  Compressor kept playing the side-chain listen, a soloed band and delta in bypass, and its oversampling filters.
- Blocks larger than the host announced in `prepareToPlay` are processed in pieces. ZL Compressor wrote past the end of
  its buffers.
- The processor no longer keeps pointers into the host's audio buffers between blocks.

### Compatibility

- Same parameters and state format as ZL Compressor: presets and sessions load unchanged.
- Installs alongside ZL Compressor (different plug-in codes and bundle identifier).

The upstream changelog is kept in [docs/UPSTREAM_CHANGELOG.md](docs/UPSTREAM_CHANGELOG.md).
