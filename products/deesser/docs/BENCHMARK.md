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
