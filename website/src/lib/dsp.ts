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
