// Every fact on the site comes from products/<code>/README.md, CHANGELOG.md and docs/ in this repository.

export const REPO = "https://github.com/pumfleet/altvane";

export type ProductCode = "eq" | "dialogue" | "compressor" | "limiter";

export interface Product {
  code: ProductCode;
  name: string;
  short: string;
  kind: string;
  version?: string; // the latest release, once there is one
  platforms: ("macOS" | "Windows" | "Linux")[];
  platformNote?: string;
  summary: string;
  close: string;
  readings: { value: string; label: string }[];
}

export const products: Product[] = [
  {
    code: "eq",
    name: "Altvane EQ",
    short: "EQ",
    kind: "Parametric and dynamic equalizer",
    version: "0.1.0",
    platforms: ["macOS", "Windows", "Linux"],
    summary:
      "An equalizer that draws the curve it applies. Every curve on screen is computed from the filter coefficients and tested against the processed audio.",
    close: "Put Altvane EQ on your next mix.",
    readings: [
      { value: "24", label: "bands, 11 shapes" },
      { value: "0.01 dB", label: "curve against processed audio" },
      { value: "6–96", label: "dB per octave" },
    ],
  },
  {
    code: "dialogue",
    name: "Altvane Dialogue",
    short: "Dialogue",
    kind: "Neural dialogue isolation",
    version: "0.1.0",
    platforms: ["macOS", "Windows"],
    platformNote: "macOS 11+ and Windows x64 for now",
    summary:
      "Pull the voice out of the room. Set the voice, the ambience and the voice's reverb on their own; the three always add up to the input, so at 0 dB it is transparent, sample for sample.",
    close: "Put Altvane Dialogue on your next edit.",
    readings: [
      { value: "3", label: "stems: voice, ambience, reverb" },
      { value: "8%", label: "of one core in Eco, M5 Max" },
      { value: "44.1–192", label: "kHz, mono or stereo" },
    ],
  },
  {
    code: "compressor",
    name: "Altvane Compressor",
    short: "Compressor",
    kind: "Compressor, upward compressor and expander",
    platforms: ["macOS", "Windows", "Linux"],
    summary:
      "Compress, inflate, expand or shape, with one soft knee, four detector styles and a side chain with its own 8-band EQ.",
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
  },
];

export const product = (code: ProductCode) => products.find((p) => p.code === code)!;

/** the GitHub releases for one product; releases are tagged <code>-vX.Y.Z */
export const releasesUrl = (code: ProductCode) => `${REPO}/releases?q=${code}-v&expanded=true`;
/** "Download 0.1.0", or just "Download" before a product has a version */
export const downloadLabel = (p: Product) => (p.version ? `Download ${p.version}` : "Download");
export const sourceUrl = (code: ProductCode) => `${REPO}/tree/main/products/${code}`;

/** release builds, from .github/release-matrix.json */
export const builds = [
  { os: "macOS", arch: "Apple silicon, Intel", formats: ["VST3", "AU", "Standalone"] },
  { os: "Windows", arch: "x64, Arm64", formats: ["VST3", "LV2", "Standalone"] },
  { os: "Linux", arch: "x86-64, Arm64", formats: ["VST3", "LV2", "Standalone"] },
] as const;
