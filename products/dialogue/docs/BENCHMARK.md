# Altvane Dialogue benchmarks

## Separation (synthetic room, 2026-10-26)

A test mix with known parts: speech convolved with a room (direct sound and reflections up to 50 ms as the "dry"
voice, an exponential late tail with T60 0.8 s as the reverb, about 0 dB direct-to-reverberant), plus rumble, 60 Hz hum
and hiss at −10 dB. Scores are SDR in dB against the true part (higher is better); High quality, Linked.

| | SDR |
|---|---|
| input vs dry voice | 6.3 |
| **voice stem vs dry voice** | **11.9** |
| voice + reverb stems vs dry + late | 13.8 |
| reverb stem vs late reverb | 1.8 |
| ambience stem vs noise | 3.1 |
| stems summed vs input | 113.8 (float precision) |

For reference, ideal ratio masks computed from the true parts (an oracle, not achievable) reach about 11–12 dB for
the dry voice and only about 3 dB for the late reverb: late reverb overlaps the voice in time and frequency, so no
per-bin mask separates it well. The DSP reverb split here gets about half of that. A model trained with a reverb
output is the planned improvement (see `models/README.md`).

Decay prior: the split was scored with the assumed T60 at 0.4, 0.7 and 1.2 s against rooms of 0.4 and 1.2 s; all
within 0.8 dB, so a fixed 0.8 s prior is used instead of a blind estimate.

## CPU (release build, M5 Max, share of one core per instance, 48 kHz)

| Quality | Linked | Stereo |
|---|---|---|
| Eco | 8% | 16% |
| High | 22% | 43% |

Real-time soak: `AltvaneDialogueRender --realtime` paced like an audio device, High/Stereo at 128-sample blocks and
Eco/Linked at 64: no late frames.

## Latency (512-sample blocks)

| Host rate | Samples | ms |
|---|---|---|
| 44.1 kHz | 3709 | 84.1 |
| 48 kHz | 3872 | 80.7 |
| 96 kHz | 7302 | 76.1 |

## Against Antinode Clear and Extract:Dialogue 2

Not measured yet: these need renders from licensed copies of both, of the same clips, at matched settings. To add
them, render each clip with `AltvaneDialogueRender` and with the other plug-ins (voice kept, ambience and reverb off),
and record SDR on the synthetic mixes and blind listening notes on real recordings here.
