# Changelog

## 0.1.0

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

### Sessions and presets

- Sessions and presets record the layout of the state they were saved in (`schema_version="1"` on the state's root),
  so a later version can upgrade them. A state without it, saved before, loads as it always has; one saved by a
  newer version loads every setting this version has and leaves the rest.
- Restoring a session or preset puts each switch exactly where it was saved. One a host had left between on and off
  (0.4 is off) went on reporting that value to the host.
- The rider fader is saved at the value it reports, so a restored session saves back unchanged. It was saved at its
  default, 0 dB, and restored a step's rounding error away from it.

### Interface

- An 8-second history of input, output and music levels, the target and floor lines (draggable), the gain and the
  detector's state; the rider fader with range handles and idle marker; eight factory presets.
