# Changelog

## Unreleased

First version of Altvane Leveler, an automatic vocal rider.

### Riding

- Rides a voice onto a target (BS.1770 loudness, −42 to 0 dB) within a range of up to ±24 dB, with a continuous
  speed (cuts twice as fast as boosts), holding through gaps between syllables and drifting to an idle gain after
  longer silences.
- Voicing detection (pitch confidence and high-band energy) keeps breaths, sibilants, noise and atonal spill from
  moving the gain, by a Guard amount; a sensitivity floor below the target ignores quieter sounds.
- 30 ms lookahead so the phrase-start move lands before the phrase; off for zero latency. It changes the latency,
  so it is not automatable.
- Music side chain (mono or stereo): the target follows the music's level relative to its own long-term level.
- Learn sets the target from the voiced loudness.

### Automation

- The rider fader is an automatable parameter. Write sends it to the host while the transport plays, in one gesture,
  about 60 times a second from the interface (so the lane is not sample-accurate); Read follows its automation; grab
  it to ride by hand in any mode.
- Loading a session restores the fader without the host taking it for an automation write.
- Presets and A/B never move the fader, and loading a preset never changes the automation mode.

### Interface

- An 8-second history of input, output and music levels, the target and floor lines (draggable), the gain and the
  detector's state; the rider fader with range handles and idle marker; eight factory presets.
