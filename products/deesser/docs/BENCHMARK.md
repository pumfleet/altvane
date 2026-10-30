# Altvane De-Esser benchmarks

## CPU (release build, AVX2, share of one core per instance, stereo, 48 kHz, 128-sample blocks)

`AltvaneDeEsserRender --bench 60 128`: 60 s of the synthetic voice, threshold −30 dB, best of three runs.

| Processing | CPU | Real time |
|---|---|---|
| Split, shelf | 0.80% | 125× |
| Split, bell | 1.15% | 87× |
| Wide | 0.49% | 203× |

Measured on an AMD Threadripper PRO 7965WX (Linux, 2026-10-26) while the machine was under heavy unrelated load
(load average about 40 on 24 cores), so single runs varied by up to 4×; the best of three is the closest to an idle
machine. Remeasure on a quiet one. Decibels are only taken when the band is within the knee of the threshold, so a
de-esser with nothing to do costs less than one that is working.

## Latency

The latency is the look-ahead, 0 to 15 ms (5 ms by default: 240 samples at 48 kHz), reported to the host and kept
when bypassed. With look-ahead at 0 the plug-in adds no latency.

## Accuracy (from the tests)

| | |
|---|---|
| Output with nothing to reduce | the input delayed by the look-ahead, bit-exact (stereo), within 10⁻⁶ (mid/side) |
| Delta + output | the delayed input, within 10⁻⁵ |
| Steady ess 12 dB over the threshold, range 24 dB | reduced by 9 to 14 dB |
| Range 4 dB | reduced by 3 to 4.3 dB |
| Split shelf, 300 Hz under an ess | moves less than 0.3 dB |
| Bell over 5–8 kHz, 16 kHz above it | moves less than 1 dB |
| Finder, esses at 4.5, 7 and 9.5 kHz | within a sixth of an octave, at 44.1 and 96 kHz |
| Learn, esses at known levels | median − 4 dB, within 1.5 dB |

## Against FabFilter Pro-DS

Not measured yet: this needs a licensed copy of Pro-DS and the same clips rendered at matched reduction. To add it,
render a set of sibilant vocals with `AltvaneDeEsserRender` and with Pro-DS (Single Vocal against Vocal, Allround
against Allround, set to the same peak gain reduction on the same esses), and record here:

- missed and false detections, counted by ear and from each plug-in's own display, on dry vocals and on full mixes;
- lisping (dulled esses) at matched reduction, in blind listening;
- what each passes when nothing should change (Altvane's output is the input, sample for sample);
- CPU per instance at 48 kHz and 128-sample blocks, on the same machine.

## Before release, by hand

The look-ahead's latency, the external side chain, host bypass and session recall are not covered by any test
against a real host. Run this script in each host below and record the result, the host version and the date in the
table at the end. Nothing here has been run yet.

### Set-up (once per host)

1. A 48 kHz session with:
   - **Vocal:** a mono track with about 20 s of a sibilant voice (or `AltvaneDeEsserRender voice out.wav`, the
     synthetic voice).
   - **Reference:** a copy of Vocal on its own track, polarity inverted, with no plug-in. Muted until step 7.
   - **Key:** a track with a different sibilant voice or hi-hats, for the side chain checks.
   - A stereo track with a vocal on it, for step 11.
2. A new instance of Altvane De-Esser on Vocal (defaults: Vocal mode, Split, Shelf, look-ahead 5 ms).
3. Show the plug-in window and its level history.

### A. De-essing and listening

1. Play Vocal and lower the threshold until the history shows reduction on the esses only (not on vowels).
2. **Trigger** plays only the detection band; **Delta** plays only what is being taken away; Off returns to the
   de-essed signal. None of the switches clicks.
3. Save the session with Delta on and reopen it: listening is Off again.

### B. Latency and look-ahead

4. Where the host shows plug-in latency, it reads 240 samples at 48 kHz with the default 5 ms look-ahead.
5. During playback set look-ahead to 15 ms, then 0 ms. The host's latency follows (720, then 0 samples) without
   restarting playback; the audio fades between delays without a click or a lasting echo.
6. Look-ahead is not offered for automation. If the host lists it anyway, note it.
7. Unmute Reference. Set the threshold to 0 dB (nothing to reduce): Vocal and Reference null to silence at
   look-ahead 0, 5 and 15 ms.

### C. Bypass

8. With the threshold low enough to reduce, toggle the plug-in's own **Bypass**, then the host's bypass for the
   insert. Both null against Reference (bypass keeps the latency), and neither clicks.

### D. Side chain

9. Route Key to the plug-in's side-chain input (see the host notes) and set Side Chain to External. The esses of Key
   now drive the reduction on Vocal: the history shows reduction where Key is sibilant, not where Vocal is.
10. Repeat with a mono and a stereo side-chain feed. With the side chain unrouted and External selected, the plug-in
    falls back to Vocal's own signal.

### E. Stereo, mid and side

11. On the stereo vocal track, set Stereo Mode to Mid, then Side, with a low threshold. Mid reduces what is in the
    centre; Side reduces only what differs between the channels. Back on Stereo, nothing is left over from Mid or
    Side (no stuck reduction on one channel).

### F. Sample rates and session recall

12. Switch the session to 44.1, 96 and 192 kHz in turn (88.2 and 176.4 kHz where the host offers them). The
    detection band and the reduction behave the same, and the latency follows the rate (5 ms each time).
13. Save, close and reopen: every control is restored, the session is not marked as modified straight after
    loading, and no automation point was written.

### Host notes

- **Logic Pro (AU):** the side chain is the **Side Chain** menu at the top of the plug-in window. Logic does not show
  plug-in latency; use step 7. Also run `auval -v aufx AvDs Altv` once.
- **Reaper (VST3):** give the track 4 channels, connect the plug-in's side-chain inputs to 3/4 in the pin connector,
  and send Key to channels 3/4. The FX chain's performance meter shows the delay (PDC).
- **Ableton Live (VST3):** pick Key in the device's side-chain section, if your version shows one; otherwise skip D
  and note it. Use step 7 for latency.
- **Cubase (VST3):** activate the side-chain button in the plug-in window's header and add a side-chain send from
  Key. Studio > VST Plug-in Manager shows latency, or use step 7.
- **Pro Tools (AAX):** releases do not include an AAX build yet. This needs one built with `-DALTVANE_FORMATS=AAX`
  and signed for Pro Tools (or Pro Tools Developer for an unsigned build). The side chain is the plug-in's key input
  selector, fed from a bus. Until then, record "no AAX build".

### Results

| Host | Version | Date | A | B | C | D | E | F | Notes |
|---|---|---|---|---|---|---|---|---|---|
| Logic Pro (AU) | | | not run | | | | | | |
| Reaper (VST3) | | | not run | | | | | | |
| Ableton Live (VST3) | | | not run | | | | | | |
| Cubase (VST3) | | | not run | | | | | | |
| Pro Tools (AAX) | | | not run | | | | | | no AAX build yet |
