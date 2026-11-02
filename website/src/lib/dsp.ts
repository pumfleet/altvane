// The figures on the site are computed, not drawn: each one runs a small piece of the math its product runs.

const SAMPLE_RATE = 48000;

export const F_MIN = 20;
export const F_MAX = 20000;

/** position 0..1 on a log frequency axis */
export const logPos = (f: number, lo = F_MIN, hi = F_MAX) => Math.log(f / lo) / Math.log(hi / lo);
export const logFreq = (t: number, lo = F_MIN, hi = F_MAX) => lo * Math.pow(hi / lo, t);

export type BandShape = "peak" | "lowShelf" | "highShelf" | "highPass";

export interface Band {
  shape: BandShape;
  freq: number;
  gain: number; // dB
  q: number;
}

interface Biquad {
  b0: number;
  b1: number;
  b2: number;
  a0: number;
  a1: number;
  a2: number;
}

/** RBJ Audio EQ Cookbook coefficients */
function coefficients({ shape, freq, gain, q }: Band): Biquad {
  const w = (2 * Math.PI * freq) / SAMPLE_RATE;
  const cos = Math.cos(w);
  const alpha = Math.sin(w) / (2 * q);
  const A = Math.pow(10, gain / 40);
  switch (shape) {
    case "peak":
      return { b0: 1 + alpha * A, b1: -2 * cos, b2: 1 - alpha * A, a0: 1 + alpha / A, a1: -2 * cos, a2: 1 - alpha / A };
    case "lowShelf": {
      const s = 2 * Math.sqrt(A) * alpha;
      return {
        b0: A * (A + 1 - (A - 1) * cos + s),
        b1: 2 * A * (A - 1 - (A + 1) * cos),
        b2: A * (A + 1 - (A - 1) * cos - s),
        a0: A + 1 + (A - 1) * cos + s,
        a1: -2 * (A - 1 + (A + 1) * cos),
        a2: A + 1 + (A - 1) * cos - s,
      };
    }
    case "highShelf": {
      const s = 2 * Math.sqrt(A) * alpha;
      return {
        b0: A * (A + 1 + (A - 1) * cos + s),
        b1: -2 * A * (A - 1 + (A + 1) * cos),
        b2: A * (A + 1 + (A - 1) * cos - s),
        a0: A + 1 - (A - 1) * cos + s,
        a1: 2 * (A - 1 - (A + 1) * cos),
        a2: A + 1 - (A - 1) * cos - s,
      };
    }
    case "highPass":
      return { b0: (1 + cos) / 2, b1: -(1 + cos), b2: (1 + cos) / 2, a0: 1 + alpha, a1: -2 * cos, a2: 1 - alpha };
  }
}

/** |H(e^jw)| of one biquad, in dB */
function magnitudeDb(c: Biquad, f: number): number {
  const w = (2 * Math.PI * f) / SAMPLE_RATE;
  const c1 = Math.cos(w);
  const c2 = Math.cos(2 * w);
  const s1 = Math.sin(w);
  const s2 = Math.sin(2 * w);
  const nr = c.b0 + c.b1 * c1 + c.b2 * c2;
  const ni = -(c.b1 * s1 + c.b2 * s2);
  const dr = c.a0 + c.a1 * c1 + c.a2 * c2;
  const di = -(c.a1 * s1 + c.a2 * s2);
  return 10 * Math.log10((nr * nr + ni * ni) / (dr * dr + di * di));
}

/** the summed response of a set of bands at frequency f, in dB */
export function response(bands: Band[], f: number): number {
  return bands.reduce((sum, band) => sum + magnitudeDb(coefficients(band), f), 0);
}

export const defaultBands = (): Band[] => [
  { shape: "highPass", freq: 32, gain: 0, q: 0.71 },
  { shape: "lowShelf", freq: 110, gain: 3, q: 0.71 },
  { shape: "peak", freq: 420, gain: -4.5, q: 1.6 },
  { shape: "peak", freq: 2800, gain: 4, q: 1.1 },
  { shape: "highShelf", freq: 9000, gain: 2, q: 0.71 },
];

/** a second setting, for a voice: clear the rumble, take out the box, lift the presence */
export const voiceBands = (): Band[] => [
  { shape: "highPass", freq: 80, gain: 0, q: 0.71 },
  { shape: "peak", freq: 260, gain: -6, q: 1.8 },
  { shape: "peak", freq: 1200, gain: 0, q: 1 },
  { shape: "peak", freq: 5200, gain: 5.5, q: 0.9 },
  { shape: "highShelf", freq: 14000, gain: -3, q: 0.71 },
];

export const presets = { default: defaultBands, voice: voiceBands };
export type Preset = keyof typeof presets;

/** the same bands with no effect: every gain at 0 dB and the high-pass below the audible range */
export const neutralBands = (bands: Band[]): Band[] =>
  bands.map((b) => (b.shape === "highPass" ? { ...b, freq: 4 } : { ...b, gain: 0 }));

/** the band the pointer tunes on the EQ figure */
export const LIVE_BAND = 3;

// ---------------------------------------------------------------------------------------------------------------
// Dialogue: three stems that always add up to the input

export interface StemRow {
  freq: number;
  input: number; // 0..1, the input's level in this band
  voice: number; // shares of the input, summing to 1
  ambience: number;
  reverb: number;
}

/** a synthetic spectrum of speech in a room: illustrative, not a measurement */
export function dialogueRows(count: number): StemRow[] {
  const rows: StemRow[] = [];
  for (let i = 0; i < count; i++) {
    const t = i / (count - 1);
    const freq = logFreq(t, 60, 16000);
    const x = logPos(freq, 60, 16000);
    // speech: formant humps between 150 Hz and 4 kHz
    const speech =
      0.62 * Math.exp(-Math.pow((x - 0.36) / 0.16, 2)) +
      0.32 * Math.exp(-Math.pow((x - 0.6) / 0.09, 2)) +
      0.18 * Math.exp(-Math.pow((x - 0.78) / 0.06, 2));
    // the room: rumble at the bottom, hiss at the top, a little everywhere
    const noise = 0.12 + 0.38 * Math.exp(-Math.pow(x / 0.13, 2)) + 0.22 * Math.pow(x, 3);
    const tail = speech * 0.34;
    const total = speech + noise + tail;
    rows.push({
      freq,
      input: Math.min(1, 0.22 + total * 0.78),
      voice: speech / total,
      ambience: noise / total,
      reverb: tail / total,
    });
  }
  return rows;
}

/** dB to linear amplitude, where -60 dB and below is Off */
export const gainOf = (db: number) => (db <= -60 ? 0 : Math.pow(10, db / 20));

// ---------------------------------------------------------------------------------------------------------------
// Compressor: the static gain computer with a soft knee (Giannoulis, Massberg & Reiss)

export interface Knee {
  threshold: number; // dB
  ratio: number;
  width: number; // dB
}

export function transfer({ threshold, ratio, width }: Knee, input: number): number {
  const over = input - threshold;
  if (2 * over < -width) return input;
  if (2 * Math.abs(over) <= width) {
    return input + ((1 / ratio - 1) * Math.pow(over + width / 2, 2)) / (2 * width);
  }
  return threshold + over / ratio;
}

// ---------------------------------------------------------------------------------------------------------------
// Limiter: a lookahead peak limiter, at a resolution of one millisecond: a gain computer, a lookahead ramp and a release

export interface Limit {
  gain: number; // dB into the limiter
  ceiling: number; // dB
  release: number; // ms
}

export const LIMIT_SECONDS = 3;
export const LIMIT_LOOKAHEAD = 2; // ms, the editor's default

/** a small seeded generator, so the programme is the same at build time and in the browser */
function mulberry32(seed: number) {
  return () => {
    seed = (seed + 0x6d2b79f5) | 0;
    let t = Math.imul(seed ^ (seed >>> 15), 1 | seed);
    t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t;
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  };
}

/**
 * the peak level of a synthetic programme, one linear value per millisecond: a 120 bpm groove of kick, snare and
 * hats over a bed, peaking about 3 dB under full scale. Illustrative, not a recording.
 */
export function programme(): Float64Array {
  const n = LIMIT_SECONDS * 1000;
  const rand = mulberry32(7);
  const out = new Float64Array(n);
  const hit = (t: number, at: number, peak: number, decay: number) => (t < at ? 0 : peak * Math.exp(-(t - at) / decay));
  for (let t = 0; t < n; t++) {
    const beat = t % 500;
    const bar = Math.floor(t / 500);
    const kick = bar % 2 === 0 ? hit(beat, 0, 0.7, 70) : hit(beat, 0, 0.16, 40);
    const snare = bar % 2 === 1 ? hit(beat, 0, 0.58, 95) : 0;
    const hats = hit(t % 125, 0, 0.12, 14);
    const bed = 0.2 + 0.05 * Math.sin((t / n) * Math.PI * 3.2) + 0.03 * Math.sin(t / 37);
    out[t] = (kick + snare + hats + bed) * (0.88 + 0.12 * rand());
  }
  const top = out.reduce((m, v) => Math.max(m, v), 0);
  return out.map((v) => (v * 0.7) / top);
}

/**
 * the limiter's gain, one linear value per millisecond. Each point's required gain brings it to the ceiling; the
 * lowest required gain within the lookahead is ramped in over the lookahead, so the gain is already down when the peak
 * arrives, and it recovers with the release. The ramp and the release only ever lower the gain below what is
 * required, so the output never passes the ceiling.
 */
export function limit(peaks: Float64Array, { gain, ceiling, release }: Limit): Float64Array {
  const n = peaks.length;
  const drive = Math.pow(10, gain / 20);
  const c = Math.pow(10, ceiling / 20);
  const L = LIMIT_LOOKAHEAD;
  const required = peaks.map((p) => Math.min(1, c / Math.max(p * drive, 1e-9)));
  // the lowest required gain in the lookahead ahead of each point
  const held = required.map((_, k) => {
    let m = 1;
    for (let j = k; j <= Math.min(n - 1, k + L); j++) m = Math.min(m, required[j]);
    return m;
  });
  const k = 1 - Math.exp(-1 / Math.max(1, release));
  const out = new Float64Array(n);
  let g = 1;
  for (let i = 0; i < n; i++) {
    // the mean of the held gains over the last lookahead: every window it averages covers point i
    let sum = 0;
    for (let j = i - L; j <= i; j++) sum += held[Math.max(0, j)];
    const ramp = sum / (L + 1);
    g = ramp < g ? ramp : g + (ramp - g) * k;
    out[i] = g;
  }
  return out;
}

export const dbOf = (x: number) => 20 * Math.log10(Math.max(x, 1e-9));

// ---------------------------------------------------------------------------------------------------------------
// De-Esser: the detection band, the Vocal weighting, the soft knee and the dynamic shelf of
// products/deesser/source/dsp (detector.hpp, gain_computer.hpp, Engine::responseDb), applied to the steady spectrum
// of one moment of a voice

export type DeEssSound = "ess" | "vowel";
export type DeEssMode = "vocal" | "allround";

export interface DeEss {
  threshold: number; // dB
  range: number; // dB
  low: number; // Hz, the detection band's low edge (the band is open at the top)
  mode: DeEssMode;
  sound: DeEssSound;
}

const DE_RATE = 48000;
const DE_KNEE = 6; // GainComputer::kKneeDb
const BUTTERWORTH4_Q = [0.5411961, 1.306563]; // the two sections of the fourth-order high-pass
const SQRT2 = Math.SQRT2;

/** tan(pi f / fs): a TPT state-variable filter is its analog prototype at the prewarped frequency */
const warp = (f: number) => Math.tan((Math.PI * Math.min(Math.max(f, 1), 0.4999 * DE_RATE)) / DE_RATE);

/** |H|^2 of a second-order high-pass or low-pass with damping k, at prewarped s = jw, cutoff wc */
function pass2(w: number, wc: number, k: number, high: boolean): number {
  const re = wc * wc - w * w;
  const im = k * wc * w;
  const num = high ? w * w : wc * wc;
  return (num * num) / (re * re + im * im);
}

/** the de-esser's high shelf, down to g: Engine::responseDb for Split, Shelf; in dB */
export function deessShelfDb(f: number, low: number, reduction: number): number {
  if (reduction <= 0) return 0;
  const g = Math.pow(10, -reduction / 20);
  const w = warp(f);
  const wp = warp(Math.min(low, 0.45 * DE_RATE)) * Math.pow(g, 0.25);
  // h = (wp^2 + k sqrt(g) wp s + g s^2) / (s^2 + k wp s + wp^2), s = jw
  const nr = wp * wp - g * w * w;
  const ni = SQRT2 * Math.sqrt(g) * wp * w;
  const dr = wp * wp - w * w;
  const di = SQRT2 * wp * w;
  return 10 * Math.log10((nr * nr + ni * ni) / (dr * dr + di * di));
}

/** GainComputer::target: the reduction wanted for a band level `over` dB above the threshold */
export function deessTarget(over: number, sibilance: number, range: number): number {
  if (over <= -0.5 * DE_KNEE) return 0;
  const t = over + 0.5 * DE_KNEE;
  const r = over < 0.5 * DE_KNEE ? (t * t) / (2 * DE_KNEE) : over;
  return Math.min(r * sibilance, range);
}

/** sibilanceConfidence: none with the band 12 dB under the body, all at level with it and above */
export function deessConfidence(bandOverBody: number): number {
  const t = Math.max(0, Math.min(1, (bandOverBody + 12) / 12));
  return t * t * (3 - 2 * t);
}

export const DEESS_LO = 120;
export const DEESS_HI = 20000;
const DEESS_BINS = 480;

export interface Spectrum {
  freq: Float64Array;
  power: Float64Array; // linear power per bin
}

/**
 * a synthetic spectrum of one moment of a voice, as a steady level per bin: illustrative, not a recording. An ess is
 * a broad hiss peaking near 7 kHz over a quiet voice; a bright vowel is a loud voice whose high harmonics sit
 * 20 to 30 dB below its body, as the README puts it.
 */
export function deessSpectrum(sound: DeEssSound): Spectrum {
  const freq = new Float64Array(DEESS_BINS);
  const power = new Float64Array(DEESS_BINS);
  for (let i = 0; i < DEESS_BINS; i++) {
    const f = logFreq(i / (DEESS_BINS - 1), DEESS_LO, DEESS_HI);
    const x = Math.log2(f / 1000);
    const hump = (centre: number, width: number) => Math.exp(-Math.pow((x - Math.log2(centre / 1000)) / width, 2));
    let db: number;
    if (sound === "ess") {
      const voice = -68 + 14 * hump(350, 1.1) + 6 * hump(1400, 0.6) - 4 * Math.max(0, x - 1);
      const hiss = -54 + 22 * hump(7200, 0.75) + 6 * hump(4600, 0.35) + 3 * hump(10500, 0.4);
      db = 10 * Math.log10(Math.pow(10, voice / 10) + Math.pow(10, hiss / 10));
    } else {
      // formants near 300 Hz, 800 Hz and 2.6 kHz, then the harmonics fall away; a little air at the top
      const body = -34 + 18 * hump(300, 0.9) + 14 * hump(800, 0.45) + 9 * hump(2600, 0.4) - 5.5 * Math.max(0, x - 1.5);
      const air = -62 + 4 * hump(8000, 1);
      db = 10 * Math.log10(Math.pow(10, body / 10) + Math.pow(10, air / 10));
    }
    freq[i] = f;
    power[i] = Math.pow(10, db / 10);
  }
  return { freq, power };
}

const powerDb = (p: number) => 10 * Math.log10(Math.max(p, 1e-12));

export interface DeEssResult {
  band: number; // dB, the detection band's level
  body: number; // dB, the body below the band
  sibilance: number; // 0..1
  reduction: number; // dB, positive
}

/** the detector and the gain computer on one steady moment: the band and the body, the weighting, the knee, the range */
export function deess(spec: Spectrum, s: DeEss): DeEssResult {
  const wc = warp(s.low);
  let band = 0;
  let body = 0;
  for (let i = 0; i < spec.freq.length; i++) {
    const w = warp(spec.freq[i]);
    band += spec.power[i] * pass2(w, wc, 1 / BUTTERWORTH4_Q[0], true) * pass2(w, wc, 1 / BUTTERWORTH4_Q[1], true);
    body += spec.power[i] * pass2(w, wc, SQRT2, false);
  }
  const bandDb = powerDb(band);
  const bodyDb = powerDb(body);
  const sibilance = s.mode === "vocal" ? deessConfidence(bandDb - bodyDb) : 1;
  return { band: bandDb, body: bodyDb, sibilance, reduction: deessTarget(bandDb - s.threshold, sibilance, s.range) };
}

/** the input and output level of each column of the spectrum, in dB, with the shelf applied at the reduction */
export function deessColumns(spec: Spectrum, low: number, reduction: number, count: number) {
  const cols: { lo: number; hi: number; input: number; output: number }[] = [];
  const per = spec.freq.length / count;
  for (let c = 0; c < count; c++) {
    let input = 0;
    let output = 0;
    for (let i = Math.floor(c * per); i < Math.floor((c + 1) * per); i++) {
      input += spec.power[i];
      output += spec.power[i] * Math.pow(10, deessShelfDb(spec.freq[i], low, reduction) / 10);
    }
    cols.push({
      lo: spec.freq[Math.floor(c * per)],
      hi: spec.freq[Math.min(spec.freq.length - 1, Math.floor((c + 1) * per))],
      input: powerDb(input),
      output: powerDb(output),
    });
  }
  return cols;
}

// ---------------------------------------------------------------------------------------------------------------
// Leveler: the rider of products/leveler/source/dsp (rider.cpp, Engine::endHop) on the levels of a synthetic vocal,
// once per 5 ms hop

export interface Ride {
  target: number; // dB
  range: number; // ±dB
  speed: number; // 0..1, 1.5 s to 40 ms
  guard: number; // 0..1
  lookahead: boolean;
}

export const RIDE_HOP = 0.005;
export const RIDE_SECONDS = 8;
const RIDE_LOOKAHEAD_HOPS = 6;
const RIDE_LEVEL_HOPS = 20; // 100 ms
const RIDE_FAST_HOPS = 4; // 20 ms
const RIDE_HOLD = 0.3;
const RIDE_ONSET = 0.03;
const RIDE_ONSET_TIME = 0.005;
const SILENCE_DB = -100;

export type HopKind = "voice" | "breath" | "room";

export interface Vocal {
  level: Float64Array; // dB per hop, BS.1770-style loudness
  kind: HopKind[];
}

/** loudnessDb: BS.1770 loudness of a mean square */
const loudness = (ms: number) => (ms > 1e-10 ? -0.691 + 10 * Math.log10(ms) : SILENCE_DB);
const meanSquare = (db: number) => Math.pow(10, (db + 0.691) / 10);

/**
 * eight seconds of a synthetic vocal, like the benchmark's: phrases spread over about 18 dB, cut into 240 ms
 * syllables with 80 ms gaps, each swelling by ±30%, a breath before each phrase, over room noise at about −70 dB.
 * Illustrative, not a recording.
 */
export function vocal(): Vocal {
  const n = Math.round(RIDE_SECONDS / RIDE_HOP);
  const level = new Float64Array(n).fill(-70);
  const kind: HopKind[] = new Array(n).fill("room");
  const phrases = [
    { at: 0.62, level: -31, syllables: 4 },
    { at: 2.44, level: -14, syllables: 4 },
    { at: 4.3, level: -26, syllables: 3 },
    { at: 5.84, level: -18, syllables: 6 },
  ];
  for (const ph of phrases) {
    // the breath, 300 ms before the phrase
    for (let t = ph.at - 0.36; t < ph.at - 0.12; t += RIDE_HOP) {
      const h = Math.round(t / RIDE_HOP);
      const u = (t - (ph.at - 0.36)) / 0.24;
      level[h] = -40 + 20 * Math.log10(0.4 + 0.6 * Math.sin(Math.PI * u));
      kind[h] = "breath";
    }
    for (let s = 0; s < ph.syllables; s++) {
      const start = ph.at + s * 0.32;
      for (let t = start; t < start + 0.24; t += RIDE_HOP) {
        const h = Math.round(t / RIDE_HOP);
        if (h >= n) break;
        const u = (t - start) / 0.24;
        // a swell of ±30% in amplitude, with 10 ms edges
        const edge = Math.min(1, (t - start) / 0.01, (start + 0.24 - t) / 0.01);
        const amp = (0.7 + 0.6 * Math.sin(Math.PI * u)) * Math.max(0.05, edge);
        level[h] = ph.level + 20 * Math.log10(amp);
        kind[h] = "voice";
      }
    }
  }
  return { level, kind };
}

/**
 * the rider's gain in dB for each hop, and the gain each hop of the input is heard with: the detection runs on the
 * input as it arrives, and with lookahead the audio is delayed by six hops, so a move is in place before its sound
 */
export function ride(v: Vocal, s: Ride): { gain: Float64Array; applied: Float64Array } {
  const n = v.level.length;
  const lo = -s.range;
  const hi = s.range;
  const boost = 1.5 * Math.pow(0.04 / 1.5, Math.max(0, Math.min(1, s.speed)));
  const cut = boost * 0.5;
  const energies = new Float64Array(RIDE_LEVEL_HOPS);
  const voiced = new Array<boolean>(RIDE_LEVEL_HOPS).fill(false);
  const gain = new Float64Array(n);
  let g = 0;
  let silent = 1e9;
  let onsetLeft = 0;
  const toward = (desired: number, time: number, scale = 1) => {
    g += (desired - g) * (1 - Math.exp(-RIDE_HOP / Math.max(time, 1e-4))) * scale;
  };
  const rideTo = (level: number, scale: number, onset: boolean) => {
    const desired = Math.max(lo, Math.min(hi, s.target - level));
    let time = desired > g ? boost : cut;
    if (onset) time = Math.min(time, RIDE_ONSET_TIME);
    toward(desired, time, scale);
  };
  for (let h = 0; h < n; h++) {
    const slot = h % RIDE_LEVEL_HOPS;
    energies[slot] = meanSquare(v.level[h]);
    let fast = 0;
    for (let i = 0; i < RIDE_FAST_HOPS; i++) fast += energies[(slot + RIDE_LEVEL_HOPS - i) % RIDE_LEVEL_HOPS];
    const fastDb = loudness(fast / RIDE_FAST_HOPS);
    const floor = s.target - 24;
    const state = fastDb < floor ? "silent" : v.kind[h] === "voice" ? "voiced" : "unvoiced";
    voiced[slot] = state === "voiced";
    let ve = 0;
    let vh = 0;
    for (let i = 0; i < RIDE_LEVEL_HOPS; i++) {
      if (voiced[i]) {
        ve += energies[i];
        vh++;
      }
    }
    const voicedDb = vh > 0 ? loudness(ve / vh) : SILENCE_DB;
    if (state === "voiced") {
      if (silent > RIDE_HOLD) onsetLeft = RIDE_ONSET;
      silent = 0;
      const onset = onsetLeft > 0;
      onsetLeft = Math.max(onsetLeft - RIDE_HOP, 0);
      rideTo(voicedDb, 1, onset);
    } else if (state === "unvoiced") {
      const k = 1 - Math.max(0, Math.min(1, s.guard));
      if (k > 0) rideTo(fastDb, k * k * k, false);
    } else {
      silent += RIDE_HOP;
      onsetLeft = 0;
      if (silent > RIDE_HOLD) toward(Math.max(lo, Math.min(hi, 0)), Math.max(2 * boost, 0.3));
    }
    g = Math.max(lo, Math.min(hi, g));
    gain[h] = g;
  }
  // the gain set at the end of a hop is ramped in over the next one, onto audio delayed by the lookahead
  const delay = s.lookahead ? RIDE_LOOKAHEAD_HOPS : 0;
  const applied = new Float64Array(n);
  for (let t = 0; t < n; t++) {
    const h = t + delay - 1;
    applied[t] = h < 0 ? 0 : gain[Math.min(n - 1, h)];
  }
  return { gain, applied };
}

/**
 * the standard deviation of the 100 ms loudness over the voiced hops, before and after: the benchmark's spread. Each
 * reading is taken over the voiced hops of its 100 ms, as the rider reads the voice.
 */
export function rideSpread(v: Vocal, applied: Float64Array): { before: number; after: number } {
  const before: number[] = [];
  const after: number[] = [];
  for (let t = RIDE_LEVEL_HOPS; t < v.level.length; t++) {
    if (v.kind[t] !== "voice") continue;
    let a = 0;
    let b = 0;
    let k = 0;
    for (let i = t - RIDE_LEVEL_HOPS + 1; i <= t; i++) {
      if (v.kind[i] !== "voice") continue;
      a += meanSquare(v.level[i]);
      b += meanSquare(v.level[i] + applied[i]);
      k++;
    }
    before.push(loudness(a / k));
    after.push(loudness(b / k));
  }
  const sd = (xs: number[]) => {
    const m = xs.reduce((s, x) => s + x, 0) / xs.length;
    return Math.sqrt(xs.reduce((s, x) => s + (x - m) * (x - m), 0) / xs.length);
  };
  return { before: sd(before), after: sd(after) };
}

// ---------------------------------------------------------------------------------------------------------------

export function formatFreq(f: number): string {
  if (f >= 1000) return `${(f / 1000).toFixed(f >= 10000 ? 1 : 2)} kHz`;
  return `${Math.round(f)} Hz`;
}

export function formatDb(db: number, digits = 1): string {
  if (db <= -60) return "Off";
  const v = db.toFixed(digits);
  if (Number(v) === 0) return `0.${"0".repeat(digits)} dB`;
  return `${db > 0 ? "+" : "−"}${Math.abs(db).toFixed(digits)} dB`;
}
