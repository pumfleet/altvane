# Altvane Leveler benchmarks

## Leveling (synthetic vocal, 2026-10-26)

**Test material:** 24 s of a synthetic sung vowel (harmonics through two formants, with vibrato).
- Seven phrases with levels spread over about 18 dB, cut into 240 ms syllables with 80 ms gaps, each swelling ±30 %.
- A breath (noise) before each phrase, over room noise at about −70 dB.
- Rendered with `AltvaneLevelerRender`, target −20 dB, guard 70 %, idle 0 dB.

**What is measured:**
- **Spread:** the standard deviation of the 100 ms loudness over the voiced hops, before and after. Lower is more
  even. Part of what remains after leveling is the swell inside each syllable, which a rider should not flatten.
- **Onset error:** at each phrase start, the gain applied to its first 10 ms compared with the gain 600 ms in.

| Settings | Spread before | Spread after |
|---|---|---|
| range ±12 dB, speed 50 %, lookahead | 7.4 dB | **2.9 dB** |
| range ±12 dB, speed 50 %, no lookahead | 7.4 dB | 3.0 dB |
| range ±12 dB, speed 20 % | 7.4 dB | 3.3 dB |
| range ±12 dB, speed 85 % | 7.4 dB | 2.7 dB |
| range ±6 dB (default), speed 50 % | 7.4 dB | 4.5 dB |

| Onset error | Mean | Worst |
|---|---|---|
| lookahead (30 ms) | **2.5 dB** | 4.1 dB |
| no lookahead | 9.8 dB | 13.2 dB |

Without lookahead, a phrase that needs a 10 dB lift starts about 10 dB too quiet. That is the late phrase start
reviewers point out in riders that only react. With lookahead it starts within a few dB of where it settles.

The tests check breaths and sibilants on their own:
- Gain moves less than 1.5 dB under a breath or a sibilant at guard 70 %, against more than 4 dB at guard 0.
- See `tests/engine_tests.cpp`.

## CPU (release build, Linux, AMD Threadripper PRO 7965WX, share of one core per instance, 48 kHz)

| | Mono | Stereo |
|---|---|---|
| lookahead | 0.3–1.0 % | 0.9–1.0 % |

Measured offline with `AltvaneLevelerRender` over 24 s, so the figures vary from run to run on a shared machine.

## Latency

| Lookahead | Samples at 48 kHz | ms |
|---|---|---|
| on | 1440 | 30.0 |
| off | 0 | 0 |

At 44.1 kHz the hop is 221 samples and the lookahead 1326 samples (30.1 ms).

## Against Waves Vocal Rider

**Not measured yet.** This needs renders of the same clips from a licensed copy, at matched settings: target, range
±6 dB, Attack Slow against speed 50 %, Vocal Sensitivity 0 against sensitivity 0.

To add the comparison:
1. Render each clip with `AltvaneLevelerRender` and with Vocal Rider (Automation Off, bounced).
2. Record here, for each:
   - the voiced level spread,
   - the onset error,
   - the gain change during breaths and sibilants,
   - blind listening notes on real vocals: lead vocal, rap, podcast, a vocal with headphone bleed.

## Before release, by hand

Write and Read are not covered by any test against a real host. For each host below, check:
1. A Write pass (play, stop, play again) records the Rider lane in touch, latch and write modes.
2. Read plays the written lane back.
3. Editing the lane in the host changes what Read plays.
4. Changing the lookahead during playback updates the host's delay compensation.

Hosts:

- **Logic Pro (AU):** not checked yet.
- **Reaper (VST3):** not checked yet.
- **Ableton Live (VST3):** not checked yet.
- **Cubase (VST3):** not checked yet.
