# Altvane Leveler architecture

Paths are relative to `products/leveler/`. Everything here is Altvane code.

## Signal path

```
input ──┬─────────────────────────────► delay (0 or 30 ms) ─► × gain (ramped per hop) ─► × output ─► bypass mix
        │                                                          ▲
        └─► K-weighting ─► levels ──┐                              │
        └─► decimate ─► YIN ────────┼─► hop state ─► Rider ────────┘   (once per 5 ms hop)
music ──► K-weighting ─► 400 ms ────┘   (target offset)
```

- `dsp/common.hpp`: the hop (5 ms), the lookahead (6 hops), `HopState` (silent, unvoiced, voiced), `Mode`
  (Off, Write, Read), the history `Column`, and BS.1770 loudness from a mean square.
- `dsp/filters`: a double-precision biquad, the BS.1770 K-weighting re-derived for any rate, and RBJ low- and
  high-pass filters.
- `dsp/voicing`: `VoicingDetector`. An anti-alias filter and decimation to about 8 kHz, then YIN's cumulative mean
  normalised difference over a 20 ms window for lags of 70–800 Hz. Periodicity is `1 − min d'`. It also gives the
  share of energy above 4 kHz over 20 ms. A hop is voiced when periodicity ≥ 0.6 and that share is < 0.6.
- `dsp/rider`: `Rider`, the gain computer, in dB once per hop.
  - **Voiced:** move toward `clamp(target − voiced level)` with the speed's time constant (cuts at half of it).
  - **Unvoiced:** the same, slowed by `(1 − guard)³`.
  - **Silent:** hold for 300 ms, then glide to idle.
  - **First 30 ms of a phrase after a silence:** catch up with a 5 ms time constant. With lookahead the output is
    still silent then, so the jump is inaudible.
- `dsp/engine`: `Engine` runs everything per sample with a hop counter, so the output does not depend on the block
  size.
  - **Detection** runs on the input as it arrives. The audio comes out of a delay line, cross-faded over 10 ms when
    the lookahead is switched.
  - **Gain** is ramped linearly across each hop.
  - **Music key:** the target follows the key's 400 ms loudness minus its 30-second memory, times the Music amount,
    clamped to ±12 dB. The memory restarts until the key's window has filled.
  - **History:** each hop's levels and state are kept until the delayed audio they describe reaches the output.
    `Column`s are then lined up with the audio the gain was applied to, and pushed to a `ui::SpscQueue` while the
    editor shows them.
  - **Learn** accumulates the energy of voiced hops. Only the audio thread writes the totals: `startLearn` asks for
    a new take, which the audio thread starts at its next hop, and `stopLearn` reports nothing until it has.
- `PluginProcessor`: parameters, state, buses, output gain, the bypass cross-fade to the delayed input, the output
  meter, and the automation write.
  - **Parameters** (`params.hpp`): the IDs are frozen, and `tests` checks them. Lookahead is not automatable: it
    changes the latency, which hosts only take from the message thread.
  - **Buses:** Input and Output are mono or stereo; Music is an optional side chain, disabled, mono or stereo.

## The rider fader, presets and automation

The rider fader (`ride`) is a plain `AudioParameterFloat` added with `addParameter` after the parameter tree is
built.

- **Outside the undo history.** The tree flushes changes through the undo manager, so engine moves written to a tree
  parameter would fill the undo history.
- **Outside presets.** `presets.cpp` gives the shell a codec whose `includes` leaves the fader out, so initialising
  doesn't reset it and its moves don't mark the preset as modified. The codec writes presets without the state's
  `Session` element.
- **Session state.** That `Session` element holds the automation mode and the fader's value. A session restores them;
  a preset (which has no `Session` element) leaves them alone. The fader is restored with `setValue`, which does not
  notify the host: a notification outside a gesture can be recorded as an automation write. The engine reads the
  fader every block, and the editor shows the engine's gain.

**Write** happens on the message thread, from the processor's 60 Hz timer:

- While the mode is Write and the transport plays, it opens one change gesture on the fader, sends every move of
  0.05 dB or more, and closes the gesture when the transport stops.
- This is how a hand on a control surface is recorded, so write, touch and latch all work.
- JUCE's VST3 wrapper ignores gestures that come from the audio thread, so the audio thread can't do this.
- So the written lane is not sample-accurate: the host stamps each move when it arrives, up to a timer tick, the
  lookahead and a host buffer or two after the audio it belongs to. A sample-accurate write would need parameter
  output from the audio thread (VST3's output parameter changes), which JUCE's wrappers don't expose for this, and AU
  has no equivalent; the README says what to expect instead.
- The same timer reports a lookahead change to the host (`setLatencySamples`).

**Read** applies the fader's value to each hop instead of the rider's.

Grabbing the fader in Off or Write overrides the engine (`manual`) until release. In Read, grabbing it edits the
parameter itself, inside a gesture.

## Threads and latency

The audio thread never waits and never allocates (`tests` counts allocations). Controls reach the engine through
atomics, and history leaves through a fixed-size single-producer, single-consumer queue that drops columns when full.
Latency is 6 hops with lookahead, which is 1440 samples at 48 kHz (30 ms), or none without it.

## Editor

`ui/editor` derives from `altvane::ui::EditorShell`, which provides the header, presets, A/B, undo, settings, scale
and frame loop.

- `ui/history_view`: draws the queued columns through `ui::HistoryRing`, `TimeAxis`, `LevelAxis` and
  `traceHistory` (`libs/ui/widgets/history`). It edits the target and sensitivity through `ui::ParamEditor` when
  their lines are dragged.
- `ui/rider_panel`: `RiderFader`, a `ui::ValueControl` with range handles and an idle marker, plus the mode chips and
  the Max, Min and Idle fields.
- `ui/control_deck`: the knobs, Learn and Lookahead.
- `ui/chrome`: the footer and the output rail.
- `ui/theme/leveler_tokens.hpp`: colours and sizes.
- Factory presets (`ui/factory_presets`) are installed once into `Presets/Factory`.

## Verification

- **`AltvaneLevelerTests`** (Catch2):
  - Engine: riding onto the target at 44.1–192 kHz in mono and stereo; range limits; hold and idle; breaths and
    sibilants under the guard; the speed range (1.5 s to 40 ms) and cuts twice as fast as boosts; the music key, mono
    and stereo; latency and impulse delay; onset with and without lookahead; nulls with nothing to ride and in
    bypass; block-size independence; Read mode; no allocation on the audio thread.
  - Processor: frozen IDs, session state (restored without notifying the host), presets and A/B leaving the fader
    alone, the Write pass and its gestures with a test play head, manual ride, latency changes, lookahead not
    automatable, bus layouts, learn (and learn started and stopped while another thread plays).
  - Editor: accessibility, undo, the host menu, the mode chips, dragging the floor line, learn and history, factory
    presets.
- **Tools:** `AltvaneLevelerRender` for listening, the gain curve and CPU; `AltvaneLevelerSnapshot` for visual review.
- **Validation:** `auval -v aufx AvLv Altv`, and pluginval at strictness 10.
- `docs/BENCHMARK.md` records how even the result is, the onset error and CPU.
