// Geometry for the six poster figures. The same functions draw the static SVG at build time and redraw it in the
// browser, so the page is complete without JavaScript and identical once it runs.

import {
  type Band,
  type DeEss,
  type Knee,
  type Limit,
  type Ride,
  type Spectrum,
  type StemRow,
  type Vocal,
  DEESS_HI,
  DEESS_LO,
  LIVE_BAND,
  dbOf,
  deess,
  deessColumns,
  limit,
  logFreq,
  logPos,
  response,
  ride,
  transfer,
} from "./dsp";

const r2 = (n: number) => Math.round(n * 100) / 100;

// ---------------------------------------------------------------------------------------------------------------
// EQ: concentric arcs about one centre. Each arc is one frequency (inner 20 Hz, outer 20 kHz); its sweep from the
// vertical is the response at that frequency, so the arcs' free ends trace the curve.

export const EQ_VIEW = { w: 1000, h: 1000 };
export const EQ_ARCS = 32;
const EQ_CENTRE = { x: 40, y: 1000 };
const EQ_R0 = 150;
const EQ_R1 = 960;
const EQ_ZERO = 50; // degrees of sweep at 0 dB
const EQ_PER_DB = 3.6;

export const eqRadius = (i: number) => EQ_R0 + ((EQ_R1 - EQ_R0) * i) / (EQ_ARCS - 1);
export const eqStroke = () => ((EQ_R1 - EQ_R0) / (EQ_ARCS - 1)) * 0.3;
export const eqArcFreq = (i: number) => logFreq(i / (EQ_ARCS - 1));
export const eqSweep = (db: number) => Math.max(4, Math.min(88, EQ_ZERO + db * EQ_PER_DB));

/** a point at `deg` degrees clockwise from straight up */
function polar(r: number, deg: number) {
  const a = (deg * Math.PI) / 180;
  return { x: EQ_CENTRE.x + r * Math.sin(a), y: EQ_CENTRE.y - r * Math.cos(a) };
}

export function eqArcPath(r: number, sweep: number): string {
  const start = polar(r, 0);
  const end = polar(r, sweep);
  return `M${r2(start.x)} ${r2(start.y)}A${r2(r)} ${r2(r)} 0 0 1 ${r2(end.x)} ${r2(end.y)}`;
}

export function eqArcs(bands: Band[], progress: (i: number) => number = () => 1) {
  return Array.from({ length: EQ_ARCS }, (_, i) => {
    const r = eqRadius(i);
    const db = response(bands, eqArcFreq(i));
    return { d: eqArcPath(r, eqSweep(db) * progress(i)), db };
  });
}

/** the 0 dB reference: a ray through where every arc ends when the EQ is flat */
export function eqZeroRay(): string {
  const a = polar(EQ_R0 - 40, EQ_ZERO);
  const b = polar(EQ_R1 + 60, EQ_ZERO);
  return `M${r2(a.x)} ${r2(a.y)}L${r2(b.x)} ${r2(b.y)}`;
}

/** where the live band's node sits: on its frequency's radius, at its gain's sweep */
export function eqNode(bands: Band[]) {
  const band = bands[LIVE_BAND];
  const r = EQ_R0 + (EQ_R1 - EQ_R0) * logPos(band.freq);
  return polar(r, eqSweep(response(bands, band.freq)));
}

/** pointer position in view units to a frequency and gain for the live band */
export function eqFromPoint(x: number, y: number) {
  const dx = x - EQ_CENTRE.x;
  const dy = EQ_CENTRE.y - y;
  const r = Math.hypot(dx, dy);
  const deg = (Math.atan2(dx, dy) * 180) / Math.PI;
  const t = Math.max(0, Math.min(1, (r - EQ_R0) / (EQ_R1 - EQ_R0)));
  const freq = logFreq(0.08 + t * 0.86);
  const gain = Math.max(-12, Math.min(10, (deg - EQ_ZERO) / EQ_PER_DB));
  return { freq, gain };
}

// ---------------------------------------------------------------------------------------------------------------
// Dialogue: one bar per band of the spectrum, split into its voice, reverb and ambience. At 0 dB on every stem the
// three segments end exactly where the input's outline does.

export const DIALOGUE_ROW = 30;
export const DIALOGUE_BAR = 15;
export const DIALOGUE_SCALE = 620;
export const dialogueView = (rows: number) => ({ w: 1000, h: rows * DIALOGUE_ROW });

export interface StemGains {
  voice: number;
  ambience: number;
  reverb: number;
}

export function dialogueBars(rows: StemRow[], g: StemGains, progress: (i: number) => number = () => 1) {
  return rows.map((row, i) => {
    // low frequencies at the bottom, like a spectrum turned on its side
    const y = (rows.length - 1 - i) * DIALOGUE_ROW + (DIALOGUE_ROW - DIALOGUE_BAR) / 2;
    const p = progress(i);
    const len = row.input * DIALOGUE_SCALE * p;
    const voice = len * row.voice * g.voice;
    const reverb = len * row.reverb * g.reverb;
    const ambience = len * row.ambience * g.ambience;
    return {
      y: r2(y),
      input: r2(len),
      voice: r2(voice),
      reverb: r2(reverb),
      ambience: r2(ambience),
    };
  });
}

// ---------------------------------------------------------------------------------------------------------------
// Compressor: input level from -60 to 0 dB across, one column per step; each column rises to the output level, with
// a hairline outline up to where the input would have been.

export const COMP_VIEW = { w: 1000, h: 1000 };
export const COMP_COLS = 30;
export const COMP_FLOOR = -60;

const levelY = (db: number) => COMP_VIEW.h * (1 - (db - COMP_FLOOR) / -COMP_FLOOR);

export function compColumns(knee: Knee, progress: (i: number) => number = () => 1) {
  const step = COMP_VIEW.w / COMP_COLS;
  const width = step * 0.4;
  return Array.from({ length: COMP_COLS }, (_, i) => {
    const input = COMP_FLOOR + (-COMP_FLOOR * (i + 1)) / COMP_COLS;
    const output = transfer(knee, input);
    const p = progress(i);
    const x = i * step + (step - width) / 2;
    const top = COMP_VIEW.h - (COMP_VIEW.h - levelY(output)) * p;
    const ghost = COMP_VIEW.h - (COMP_VIEW.h - levelY(input)) * p;
    return {
      x: r2(x),
      w: r2(width),
      top: r2(top),
      ghost: r2(ghost),
      over: input > knee.threshold - knee.width / 2,
    };
  });
}

export const compThresholdY = (knee: Knee) => r2(levelY(knee.threshold));

export function compFromY(y: number): number {
  const db = COMP_FLOOR + -COMP_FLOOR * (1 - y / COMP_VIEW.h);
  return Math.max(-48, Math.min(-3, Math.round(db)));
}

// ---------------------------------------------------------------------------------------------------------------
// Limiter: three seconds of a programme as a waveform, one column per slice of time, mirrored about its centre line.
// Each column reaches the output's peak in that slice, with a hairline outline out to where the gained input would
// have gone; the gain reduction hangs from the top rule.

export const LIMIT_VIEW = { w: 1000, h: 900 };
export const LIMIT_COLS = 60;
const LIMIT_CENTRE = 560;
const LIMIT_FULL = 300; // half-height of a full-scale peak
const LIMIT_MAX = 340; // where an outline is cut off
const LIMIT_PER_DB = 12.5; // gain reduction, down from the top rule
const LIMIT_GR_MAX = 160;

export function limitColumns(peaks: Float64Array, s: Limit) {
  const gains = limit(peaks, s);
  const drive = Math.pow(10, s.gain / 20);
  const step = LIMIT_VIEW.w / LIMIT_COLS;
  const width = step * 0.4;
  const span = peaks.length / LIMIT_COLS;
  return Array.from({ length: LIMIT_COLS }, (_, i) => {
    let input = 0;
    let output = 0;
    let least = 1;
    for (let t = Math.floor(i * span); t < Math.floor((i + 1) * span); t++) {
      input = Math.max(input, peaks[t] * drive);
      output = Math.max(output, peaks[t] * drive * gains[t]);
      least = Math.min(least, gains[t]);
    }
    const out = output * LIMIT_FULL;
    const ghost = Math.min(LIMIT_MAX, input * LIMIT_FULL);
    return {
      x: r2(i * step + (step - width) / 2),
      w: r2(width),
      out: r2(out),
      ghost: r2(Math.max(out, ghost)),
      reduction: r2(Math.min(LIMIT_GR_MAX, -dbOf(least) * LIMIT_PER_DB)),
      db: dbOf(least),
    };
  });
}

/** the y of a level above the centre line, for the ceiling's lines */
export const limitLevelY = (db: number) => r2(Math.pow(10, db / 20) * LIMIT_FULL);
export const LIMIT_AXIS = { centre: LIMIT_CENTRE, full: LIMIT_FULL, perDb: LIMIT_PER_DB };

/** the most gain reduction in a set of columns, in dB (negative) */
export const limitMost = (cols: { db: number }[]) => Math.min(0, ...cols.map((c) => c.db));

// ---------------------------------------------------------------------------------------------------------------
// De-Esser: one moment of a voice as a spectrum, low frequencies on the left, one column per slice. Each column
// stands up to what is heard, and what the shelf takes away sits on top of it in the spot ink. The detection band
// is the rule from its low edge to the right.

export const DEESS_VIEW = { w: 1000, h: 800 };
export const DEESS_COLS = 40;
const DEESS_TOP = 80; // 0 dB
const DEESS_FLOOR = -60;
export const DEESS_RULE = 36; // the detection band's rule

const deessY = (db: number) => DEESS_TOP + ((DEESS_VIEW.h - DEESS_TOP) * Math.min(-db, -DEESS_FLOOR)) / -DEESS_FLOOR;
export const deessX = (f: number) => r2(DEESS_VIEW.w * logPos(f, DEESS_LO, DEESS_HI));
export const deessFreqAt = (x: number) => logFreq(Math.max(0, Math.min(1, x / DEESS_VIEW.w)), DEESS_LO, DEESS_HI);

/** the columns at a setting; `amount` scales the reduction, for the draw-in */
export function deessFigure(spec: Spectrum, s: DeEss, amount = 1) {
  const result = deess(spec, s);
  const step = DEESS_VIEW.w / DEESS_COLS;
  const width = step * 0.5;
  const cols = deessColumns(spec, s.low, result.reduction * amount, DEESS_COLS).map((c, i) => {
    const top = deessY(c.input);
    const out = deessY(c.output);
    return {
      x: r2(i * step + (step - width) / 2),
      w: r2(width),
      top: r2(top),
      out: r2(out),
      taken: r2(Math.max(0, out - top)),
      body: r2(DEESS_VIEW.h - out),
    };
  });
  return { cols, result, edge: deessX(s.low) };
}

// ---------------------------------------------------------------------------------------------------------------
// Leveler: eight seconds of a vocal, one column per 100 ms. Each column marks the level going in (a spot outline)
// and the level heard (ink), joined by the rider's move; the target is the ink rule they gather on.

export const LEVEL_VIEW = { w: 1000, h: 700 };
export const LEVEL_COLS = 80;
const LEVEL_TOP_DB = -6;
const LEVEL_FLOOR_DB = -46;
export const LEVEL_MARK = 7; // the marks' height

export const rideY = (db: number) =>
  r2((LEVEL_VIEW.h * (LEVEL_TOP_DB - Math.max(LEVEL_FLOOR_DB, Math.min(LEVEL_TOP_DB, db)))) / (LEVEL_TOP_DB - LEVEL_FLOOR_DB));
export const rideDbAt = (y: number) => LEVEL_TOP_DB - ((LEVEL_TOP_DB - LEVEL_FLOOR_DB) * y) / LEVEL_VIEW.h;

const meanSq = (db: number) => Math.pow(10, db / 10);
const db10 = (p: number) => 10 * Math.log10(Math.max(p, 1e-12));

/** which columns hold a sound, and whether it is the voice or a breath; the room's columns stay empty */
export function levelKinds(v: Vocal) {
  const per = v.level.length / LEVEL_COLS;
  return Array.from({ length: LEVEL_COLS }, (_, i) => {
    const kinds = v.kind.slice(Math.floor(i * per), Math.floor((i + 1) * per));
    return kinds.includes("voice") ? "voice" : kinds.includes("breath") ? "breath" : "room";
  });
}

/** each column's level in and out, over the hops of its kind; `amount` scales the gain, for the draw-in */
export function levelColumns(v: Vocal, s: Ride, amount = 1) {
  const { applied } = ride(v, s);
  const kinds = levelKinds(v);
  const per = v.level.length / LEVEL_COLS;
  const step = LEVEL_VIEW.w / LEVEL_COLS;
  const width = step * 0.62;
  const cols = kinds.map((kind, i) => {
    let a = 0;
    let b = 0;
    let n = 0;
    for (let h = Math.floor(i * per); h < Math.floor((i + 1) * per); h++) {
      if (v.kind[h] !== kind) continue;
      a += meanSq(v.level[h]);
      b += meanSq(v.level[h] + applied[h] * amount);
      n++;
    }
    const input = n ? db10(a / n) : LEVEL_FLOOR_DB;
    const output = n ? db10(b / n) : LEVEL_FLOOR_DB;
    return {
      kind,
      cx: r2(i * step + step / 2),
      x: r2(i * step + (step - width) / 2),
      w: r2(width),
      input: r2(rideY(input) - LEVEL_MARK / 2),
      output: r2(rideY(output) - LEVEL_MARK / 2),
    };
  });
  return { cols, applied };
}
