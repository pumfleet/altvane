# Changelog

## 0.1.0

First release of Altvane De-Esser.

### Processing

- Threshold and range on the level of a detection band (a fourth-order high-pass at its low edge, an optional
  low-pass at its high edge), with a 6 dB soft knee
- Vocal mode, which weighs each moment's band level against the body of the voice below it so bright vowels pass
  and esses do not; Allround mode for mixes, buses and instruments
- Wide band, or split band as a dynamic high shelf or a dynamic bell over the detection band; every mode is the
  input, sample for sample, whenever nothing is being reduced
- Look-ahead up to 15 ms (reported as latency, cross-faded when changed), program-dependent release
- Stereo with link, mid only or side only; external side chain; mono and stereo buses
- Trigger and Delta listen modes, output gain, bypass at constant latency

### Interface

- Level history: the signal, the detection band, what is taken from each ess, gain reduction and a draggable
  threshold, over 2, 4 or 8 seconds, with a hover readout and freeze
- Detection band on a live spectrum with the esses' own average spectrum, edges and band dragged directly, and the
  shelf or bell response drawn as it moves
- Sibilance finder: marks where this voice's esses are loudest and fits the band to them with a click
- Learn: sets the threshold from the esses heard in a passage
- Presets with a factory set, A/B compare, undo/redo, interface size and accent colour, keyboard control, screen-reader
  support and host parameter menus
