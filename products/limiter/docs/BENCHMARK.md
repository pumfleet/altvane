# Altvane Limiter benchmarks

What is measured here comes from the tests, not from timings: the numbers are the same on any machine. CPU use is not
listed yet; it needs measuring on a quiet machine (the development VM's timings are not trustworthy).

## Latency

The engine's lookahead is a fixed delay, so only oversampling and the sample rate change the latency. Reported to
the host, and kept when bypassed.

| Oversampling | 44.1 kHz | 48 kHz | 96 kHz |
|---|---|---|---|
| Off | 423 samples (9.59 ms) | 442 (9.21 ms) | 694 (7.23 ms) |
| 2× | 487 (11.04 ms) | 506 (10.54 ms) | 758 (7.90 ms) |
| 4× | 495 (11.22 ms) | 514 (10.71 ms) | 766 (7.98 ms) |
| 8× | 499 (11.32 ms) | 518 (10.79 ms) | 770 (8.02 ms) |
| 16× | 501 (11.36 ms) | 520 (10.83 ms) | 772 (8.04 ms) |
| 32× | 502 (11.38 ms) | 521 (10.85 ms) | 773 (8.05 ms) |

## The ceiling

A drum loop with a bright sine near the top of the band (0.23 of the sample rate), pushed 12 dB into a −1 dB ceiling
for 3 s. The sample peak never passes the ceiling in either mode. The table gives the highest true peak above the
ceiling, read by an independent 16× interpolation (a 128-tap Kaiser-windowed sinc, longer than the engine's own
estimator).

| Oversampling | Sample peak mode, 44.1 / 48 / 96 kHz | True peak mode, 44.1 / 48 / 96 kHz |
|---|---|---|
| Off | +1.31 / +1.28 / +1.57 dB | +0.08 / +0.12 / +0.17 dB |
| 2× | +0.38 / +0.32 / +0.45 dB | +0.04 / +0.05 / +0.04 dB |
| 4× | +0.20 / +0.09 / +0.11 dB | +0.04 / +0.01 / +0.06 dB |
| 8× | +0.11 / +0.09 / +0.11 dB | +0.03 / +0.03 / +0.04 dB |
| 16× | +0.11 / +0.07 / +0.09 dB | +0.03 / +0.01 / +0.04 dB |
| 32× | +0.11 / +0.06 / +0.09 dB | +0.03 / +0.01 / +0.04 dB |

For a true-peak ceiling, use true peak mode; with oversampling off, leave about 0.2 dB of margin.

Both tables come from `AltvaneLimiterTests "[.facts]"` (`tests/golden_tests.cpp`), measured on 2026-10-30 with the
engine at ZL Limiter `412c844`. The golden tests check the latencies exactly and the true peak mode within
+0.25 dB on every CI run.

## Loudness meter

Checked against the minimum-requirements test signals of EBU Tech 3341 and Tech 3342 (2023), synthesised from the
descriptions in their Table 1, at 44.1, 48 and 96 kHz (cases 10, 11, 13 and 14 at 44.1 and 48 kHz). All within the
specified tolerances.

| Cases | What | Tolerance |
|---|---|---|
| 3341: 1, 2 | momentary, short-term and integrated loudness of steady tones | ±0.1 LU |
| 3341: 3, 4, 5 | integrated loudness through the relative and absolute gates | ±0.1 LU |
| 3341: 9, 12 | short-term and momentary loudness, every reading | ±0.1 LU |
| 3341: 10, 11, 13, 14 | maximum short-term and momentary loudness of bursts at any offset | ±0.1 LU |
| 3341: 15–23 | maximum true peak of tones and bursts between the samples | +0.2 / −0.4 dB |
| 3342: 1–4 | loudness range, and the same with the signal repeated | ±1 LU |

Not covered: 3341 case 6 (5.0 channels; the meter is mono or stereo) and the authentic programme cases (3341: 7, 8;
3342: 5, 6), whose files are not ours to ship.
