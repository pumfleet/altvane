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

Write, Read and the lookahead's latency are not covered by any test against a real host. Run this script in each host
below and record the result, the host version and the date in the table at the end. Nothing here has been run yet.

### Set-up (once per host)

1. A 48 kHz session with two mono audio tracks:
   - **Vocal:** about 30 s of voice with quiet and loud phrases and a few seconds of silence in the middle. A spoken
     clip is fine. The first phrase should start at least 2 s in, after silence.
   - **Reference:** the same clip with no plug-in and its polarity inverted, muted until step 11.
2. An instrumental (stereo, a chorus louder than the verse) on a third track or bus, for the Music checks.
3. A new instance of Altvane Leveler on Vocal, then: range ±12 dB, Speed 50 %, Lookahead on, mode Off.
4. Show the plug-in window, the Rider automation lane of Vocal, and the host's undo history if it has one.

### A. Write

1. Set the mode to **Write**. Arm the **Rider** parameter for automation in the host's **touch** mode (see the
   host notes). Play from the start for about 15 s, then stop.
   - The Rider lane now has a curve that follows the plug-in's fader during the pass.
   - The plug-in's undo history (Cmd+Z in the plug-in) has no Rider steps.
   - The fader's cap shows the write outline while the transport plays and loses it on stop.
2. Start playback again from 10 s and stop at 20 s.
   - The lane is rewritten from 10 to 20 s only; the curve before 10 s is unchanged.
   - In touch mode the host leaves its write state on stop: there is no frozen, flat line after 20 s.
3. Repeat 1–2 in **latch** mode, then in **write** mode (the host's names are in the notes). Each records the pass.
4. Stop the transport and drag the plug-in's fader by hand. Nothing is written while stopped.

### B. Read

5. Set the mode to **Read** and play from the start.
   - The fader follows the lane, and the plug-in's gain readout matches the lane's value within 0.1 dB.
   - Grabbing the fader during playback edits the lane (if the host is writing) or snaps back to it on release.
6. Draw a step into the lane by hand: −10 dB from 5 s to 7 s. Play over it: the output meter drops by 10 dB there.
7. Set the mode to **Off** and play: the lane no longer moves the fader (the engine rides again).

### C. How late written moves land

8. Make a clip of 3 s of silence followed by a phrase at about −40 LUFS (it needs a large boost), Lookahead on.
   Write a pass over it in touch mode.
9. Zoom in on the phrase start. Measure the time from the start of the phrase in the audio to the point where the
   written lane starts to rise. Record it in the table.
   - The fader is sent from the interface thread about 60 times a second, and the host stamps each move when it
     arrives. Expect the lane to lag the audio by up to about the lookahead (30 ms) plus a host buffer or two. A
     host that delay-compensates automation writes will show less.
10. Repeat 8–9 with Lookahead off and record that too.

### D. Lookahead and latency

11. Unmute Reference and set Vocal's range to 0 dB (nothing to ride). Vocal and Reference null (silence) with
    Lookahead on, and still null after switching Lookahead off and on during playback. A short click at the switch
    is acceptable; a lasting echo or comb filtering is not.
12. Where the host shows plug-in latency, it reads 1440 samples (30 ms at 48 kHz) with Lookahead on and 0 off.
13. Lookahead is not offered for automation (it changes the latency). If the host lists it anyway, note it.

### E. Session recall

14. With the mode on Read and a written lane, save the session, close it and reopen it.
    - The mode is Read again and the fader follows the lane.
    - Loading wrote nothing: there is no new automation point at the session start or the playhead, and the session
      is not marked as modified straight after loading.
15. Load a factory preset: the mode and the fader stay where they were.

### F. Music side chain

16. Route the instrumental to the plug-in's Music input (see the host notes) and set Music to 100 %. Play through
    the louder chorus: the dashed target line in the history rises with it. Repeat with a mono send.

### Host notes

- **Logic Pro (AU):**
  - Automation modes are in the track header (Touch, Latch, Write, Read); press A to show automation and choose
    Altvane Leveler > Rider in the lane's parameter menu.
  - Music: the **Side Chain** menu at the top of the plug-in window.
  - Logic does not show plug-in latency; use step 11.
  - Also run `auval -v aufx AvLv Altv` once.
- **Reaper (VST3):**
  - Rider lane: the track's envelope button, tick Rider and **Arm**. The automation mode is in the same window
    (Touch, Latch, Write; Read to play back).
  - Music: give the track 4 channels, open the plug-in's pin connector and connect the Music inputs to 3/4, then
    send the instrumental to channels 3/4.
  - Latency: the FX chain's performance meter shows the plug-in's delay (PDC).
- **Ableton Live (VST3):**
  - Live has no touch or latch: press **Automation Arm** and record in the Arrangement. Steps 1–3 become: record a
    pass, then record over the middle; the rest of the lane stays.
  - Read is Live's normal playback of the lane; turning automation off for the parameter (the red LED) is Off.
  - Music: if the device shows a side-chain section, pick the instrumental there; otherwise skip F and note it.
  - Latency: use step 11.
- **Cubase (VST3):**
  - **W** and **R** on the track; the automation mode (Touch, Auto-Latch, Overwrite) is in the project window's
    toolbar.
  - Music: activate the side-chain button in the plug-in window's header and add a side-chain send from the
    instrumental.
  - Latency: Studio > VST Plug-in Manager lists it, or use step 11.
- **Pro Tools (AAX):**
  - Releases do not include an AAX build yet. This needs one built with `-DALTVANE_FORMATS=AAX` and signed for
    Pro Tools (or Pro Tools Developer for an unsigned build). Until then, record "no AAX build".
  - Automation modes are on the track (Touch, Latch, Write, Read); enable Rider in the plug-in's automation window.
  - Music: the plug-in's key input selector, fed from a bus.
  - Latency: the track's delay compensation readout in the Mix window.

### Results

| Host | Version | Date | A | B | C (lag, on / off) | D | E | F | Notes |
|---|---|---|---|---|---|---|---|---|---|
| Logic Pro (AU) | | | not run | | | | | | |
| Reaper (VST3) | | | not run | | | | | | |
| Ableton Live (VST3) | | | not run | | | | | | |
| Cubase (VST3) | | | not run | | | | | | |
| Pro Tools (AAX) | | | not run | | | | | | no AAX build yet |
