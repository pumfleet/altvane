// Every fact on the site comes from products/<code>/README.md, CHANGELOG.md and docs/ in this repository.

export const REPO = "https://github.com/pumfleet/altvane";

export type ProductCode = "eq" | "dialogue" | "compressor" | "limiter" | "deesser" | "leveler";

export interface Product {
  code: ProductCode;
  name: string;
  short: string;
  kind: string;
  /** the latest GitHub release, tagged <code>-v<version>: set it only once that release exists */
  version?: string;
  platforms: ("macOS" | "Windows" | "Linux")[];
  platformNote?: string;
  summary: string;
  close: string;
  readings: { value: string; label: string }[];
  /** the editor's picture in public/images, which is also the page's social image */
  editor?: { src: string; width: number; height: number; alt: string };
}

export const products: Product[] = [
  {
    code: "eq",
    name: "Altvane EQ",
    short: "EQ",
    kind: "Parametric and dynamic equalizer",
    platforms: ["macOS", "Windows", "Linux"],
    summary:
      "An equalizer that draws the curve it applies. Every curve on screen is computed from the filter coefficients and tested against the processed audio.",
    close: "Put Altvane EQ on your next mix.",
    readings: [
      { value: "24", label: "bands, 11 shapes" },
      { value: "0.01 dB", label: "curve against processed audio" },
      { value: "6–96", label: "dB per octave" },
    ],
    editor: {
      src: "/images/eq-editor.png",
      width: 1040,
      height: 620,
      alt: "The Altvane EQ editor: a dark frequency graph with six numbered band nodes on a white response curve, an analyzer below, and an output meter on the right.",
    },
  },
  {
    code: "dialogue",
    name: "Altvane Dialogue",
    short: "Dialogue",
    kind: "Neural dialogue isolation",
    platforms: ["macOS", "Windows"],
    platformNote: "macOS 11+ and Windows x64 for now",
    summary:
      "Pull the voice out of the room. Set the voice, the ambience and the voice's reverb on their own; the three always add up to the input, so at 0 dB it is transparent, sample for sample.",
    close: "Put Altvane Dialogue on your next edit.",
    readings: [
      { value: "3", label: "stems: voice, ambience, reverb" },
      { value: "70.6 ms", label: "latency at 48 kHz, 512-sample blocks" },
      { value: "44.1–192", label: "kHz, mono or stereo" },
    ],
    editor: {
      src: "/images/dialogue-editor.png",
      width: 1800,
      height: 1160,
      alt: "The Altvane Dialogue editor: a dark spectrum with the voice layer in cream over blue ambience and violet reverb, and three knobs below labelled Voice at 0.0 dB, Ambience at −20.0 dB and Reverb at −10.0 dB.",
    },
  },
  {
    code: "compressor",
    name: "Altvane Compressor",
    short: "Compressor",
    kind: "Compressor, upward compressor and expander",
    platforms: ["macOS", "Windows", "Linux"],
    summary:
      "Compress, inflate, expand or shape, with one soft knee, four styles and a side chain with its own 8-band EQ.",
    close: "Put Altvane Compressor on your next mix.",
    readings: [
      { value: "4", label: "directions: compress, inflate, expand, shape" },
      { value: "20 ms", label: "lookahead, or negative" },
      { value: "8×", label: "oversampling" },
    ],
  },
  {
    code: "limiter",
    name: "Altvane Limiter",
    short: "Limiter",
    kind: "True-peak limiter and loudness meter",
    version: "0.1.0",
    platforms: ["macOS", "Windows", "Linux"],
    summary:
      "As loud as the delivery allows, and not a hair over the ceiling. True-peak limiting with a loudness meter that keeps counting with the editor closed, the delivery targets on screen, and Match target to land on one in a step.",
    close: "Put Altvane Limiter on your next master.",
    readings: [
      { value: "32×", label: "oversampling, true peak or sample peak" },
      { value: "5 ms", label: "lookahead, at a fixed latency" },
      { value: "8", label: "delivery targets, Streaming to ATSC A/85" },
    ],
    editor: {
      src: "/images/limiter-editor.png",
      width: 2240,
      height: 1400,
      alt: "The Altvane Limiter editor: a dark scrolling display with the output's waveform pressed against a dashed ceiling line at −1.0 dBTP, red gain reduction hanging from the top and a gold short-term loudness line below; a loudness panel reading −11.6 LUFS integrated against a −14 LUFS streaming target with a Match target button; true-peak, gain-reduction and loudness meters on the right; and a control card with Character, Gain, Ceiling and timing controls.",
    },
  },
  {
    code: "deesser",
    name: "Altvane De-Esser",
    short: "De-Esser",
    kind: "De-esser and sibilance finder",
    version: "0.1.0",
    platforms: ["macOS", "Windows", "Linux"],
    summary:
      "A de-esser that knows an ess from a bright vowel. It shows what it takes from each ess, finds where a voice's sibilance lives, and with nothing to reduce, its output is the input, sample for sample.",
    close: "Put Altvane De-Esser on your next vocal.",
    readings: [
      { value: "2", label: "detection modes: Vocal and Allround" },
      { value: "15 ms", label: "look-ahead at most, reported as latency" },
      { value: "1–16", label: "kHz, where the detection band starts" },
    ],
    editor: {
      src: "/images/deesser-editor.png",
      width: 2000,
      height: 1280,
      alt: "The Altvane De-Esser editor: a dark level history of a voice with the sibilance taken from each ess in apricot, red gain reduction hanging from the top and a threshold line at −31.0 dB; below, the detection band from 5.50 kHz on a spectrum with a shelf curve and a pill reading Sibilance at 7.91 kHz, Threshold and Range knobs with a Learn button, and Mode, Band, Shape and Listen switches set to Vocal, Split and Shelf.",
    },
  },
  {
    code: "leveler",
    name: "Altvane Leveler",
    short: "Leveler",
    kind: "Automatic vocal rider",
    platforms: ["macOS", "Windows", "Linux"],
    summary:
      "A fader that rides the vocal for you. It lifts the quiet phrases, pulls back the loud ones and leaves breaths, sibilants and silence alone; with lookahead, each move is in place before its phrase starts.",
    close: "Put Altvane Leveler on your next vocal.",
    readings: [
      { value: "30 ms", label: "lookahead, or off for zero latency" },
      { value: "±24 dB", label: "range at most" },
      { value: "2.5 dB", label: "mean phrase-start error with lookahead, 9.8 dB without" },
    ],
    editor: {
      src: "/images/leveler-editor.png",
      width: 1920,
      height: 1240,
      alt: "The Altvane Leveler editor: a dark 8-second history of a vocal's level with a TARGET line at −20.0 dB, a dashed FLOOR line at −44.0 dB and the music's level in orange, the gain below it; on the right the rider fader at +2.5 dB with Off, Write and Read switches and Max, Min and Idle values; and below, Target, Speed, Sensitivity, Guard and Music knobs with Learn and Lookahead buttons.",
    },
  },
];

export const product = (code: ProductCode) => products.find((p) => p.code === code)!;

export const sourceUrl = (code: ProductCode) => `${REPO}/tree/main/products/${code}`;

/**
 * a product's primary action. A released product links to its GitHub release (tagged <code>-vX.Y.Z) and says
 * "Download 0.1.0". One without a release yet is still free and complete in the repository, so its action goes to
 * the build instructions in its README and says so, rather than to a release page with nothing on it.
 */
export interface GetAction {
  href: string;
  label: string;
  /** the label with the product's name, for an action that stands on its own */
  named: string;
  arrow: "down" | "out";
}

export function getAction(p: Product): GetAction {
  if (p.version) {
    return {
      href: `${REPO}/releases/tag/${p.code}-v${p.version}`,
      label: `Download ${p.version}`,
      named: `Download ${p.name} ${p.version}`,
      arrow: "down",
    };
  }
  return {
    href: `${REPO}/blob/main/products/${p.code}/README.md#build-from-source`,
    label: "Build from source",
    named: `Build ${p.name} from source`,
    arrow: "out",
  };
}

/** where a product comes from, in a sentence */
export function getNote(p: Product): string {
  const platforms = p.platformNote ?? "macOS, Windows and Linux";
  return p.version
    ? `Free, for ${platforms}. From GitHub Releases.`
    : `Free, for ${platforms}. There is no release build yet; its README shows how to build it.`;
}

/** release builds, from .github/release-matrix.json */
export const builds = [
  { os: "macOS", arch: "Apple silicon, Intel", formats: ["VST3", "AU", "Standalone"] },
  { os: "Windows", arch: "x64, Arm64", formats: ["VST3", "LV2", "Standalone"] },
  { os: "Linux", arch: "x86-64, Arm64", formats: ["VST3", "LV2", "Standalone"] },
] as const;
