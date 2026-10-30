# Product

<!-- impeccable:product-schema 1 -->

## Platform

web

## Stack

Astro, static output, in `website/` inside the monorepo. Chosen by the user.

## Users

Audio engineers, mixers, producers and post-production editors who work in a DAW and pick plug-ins by ear and by
trust. They arrive wanting to know what a plug-in does, whether it runs on their system and in their host, and where to
get it.

## Product Purpose

Altvane makes six audio plug-ins: Altvane EQ (parametric and dynamic equalizer), Altvane Dialogue (neural dialogue
isolation), Altvane Compressor (compressor, upward compressor and expander), Altvane Limiter (true-peak limiter and
loudness meter), Altvane De-Esser (de-esser and sibilance finder) and Altvane Leveler (automatic vocal rider). The
website introduces the brand, gives each product its own page, and gets visitors to a free download, or to the
source and its build instructions where there is no release yet.

## Positioning

The plug-ins are free and open source (AGPL-3.0), and their interfaces are engineered for precision. EQ's curves are
computed from the engine's real filter coefficients and tested against the processed audio, to within 0.01 dB. Dialogue's
stems always add up to the input, so at 0 dB it passes audio through unchanged. The De-Esser's output is the input,
sample for sample, whenever it has nothing to reduce. The interfaces stay minimal and favour direct manipulation.

## Operating Context

Visitors are in a studio or an edit suite, and often reach the site from a forum, a GitHub link or a recommendation.
They need to know the formats (VST3, AU, LV2, Standalone; no AAX builds yet), the platforms (macOS, Windows, Linux;
Dialogue is macOS 11+ on Apple silicon and Intel, and Windows x64, for now: `products/dialogue/PLATFORMS`) and the
sample rates. They evaluate a plug-in by trying it in their host.

## Capabilities and Constraints

- The primary action is a free download from GitHub Releases (`github.com/pumfleet/altvane`). Each product also links
  to its source.
- Releases are tagged per product (`<code>-vX.Y.Z`: `eq`, `dialogue`, `compressor`, `limiter`, `deesser`, `leveler`).
  Released so far: Altvane Limiter 0.1.0 (`limiter-v0.1.0`) and Altvane De-Esser 0.1.0 (`deesser-v0.1.0`), both
  marked as pre-releases on GitHub. EQ, Dialogue, Compressor and Leveler have no release yet (EQ's and Dialogue's
  CHANGELOGs name a 0.1.0, but it is not tagged).
- A product shows a version only once its GitHub release exists (`gh release list`). Every product is presented as
  available: a released one says "Download 0.1.0" and links to its release; one without a release says "Build from
  source" and links to the build instructions in its README, never to an empty release page.
- Release builds: macOS (arm64, x86-64; VST3, AU, Standalone), Windows (x64, arm64; VST3, LV2, Standalone) and Linux
  (x86-64, arm64; VST3, LV2, Standalone).
- Every product fact comes from `products/<code>/README.md`, `CHANGELOG.md` and `docs/` (`ARCHITECTURE.md`,
  `BENCHMARK.md`). Comparisons with other vendors' plug-ins are not measured yet, so the site makes none.

## Brand Commitments

- Name: Altvane. Products are named "Altvane EQ", "Altvane Dialogue", "Altvane Compressor", "Altvane Limiter",
  "Altvane De-Esser" and "Altvane Leveler".
- The plug-in UIs set Inter and use a dark editor with per-product accent colours. The website's own visual world is
  free to be designed.
- The brand is the lowercase wordmark "altvane" alone, with no symbol or logo mark. The weather-vane mark in `libs/ui` is a
  placeholder the website does not use.
- Voice: Altvane speaks as its own plug-in vendor. It is assertive and first-party ("Nothing approximate."), and every
  claim is backed by the engineering.
- The website does not attribute or mention ZL Audio, ZL Equalizer, ZL Compressor or ZL Limiter. The AGPL notices in `LICENSE.md` and
  `NOTICE.md` in the repository keep their credits, and the site links to them.
- The user's direction for the site: very light, minimalist, extremely premium, very geometric, and not like other
  plug-in sites.

## Evidence on Hand

- Screenshots (`products/<code>/docs/screenshot.png`, copied to `website/public/images/<code>-editor.png`): EQ,
  Dialogue, De-Esser and Leveler; the Limiter's is rendered by `AltvaneLimiterSnapshot` at 2×. Dialogue's predates
  Mid/Side and shows the old 80.0 ms latency, and its caption says so. The Compressor has only a design mock
  (`products/compressor/docs/design/mock.html`), so its page has no plate.
- Measured facts in the READMEs, tests and `docs/BENCHMARK.md`:
  - EQ: curve accuracy within 0.01 dB of the processed audio in the zero-latency modes.
  - Dialogue: latency 70.6 ms at 48 kHz (64- to 512-sample blocks), 74.1 ms at 44.1 kHz, 71.4 ms at 96 kHz; voice
    stem 11.9 dB SDR against the dry voice on a synthetic room (input 6.3 dB); stems summed against the input
    113.8 dB. Its CPU with the shipping C++ network has not been measured on a Mac yet; the old ONNX Runtime figures
    (8%/22%/16%/43% on an M5 Max) no longer describe it and are not used.
  - Limiter: loudness within ±0.1 LU on EBU Tech 3341 cases; gates exact to 0.01 LU.
  - De-Esser: output bit-exact with nothing to reduce (stereo); finder within a sixth of an octave; Learn within
    1.5 dB; look-ahead 0–15 ms (5 ms default). CPU was measured on a heavily loaded machine and is not quoted.
  - Leveler: voiced level spread 7.4 dB to 2.9 dB (±12 dB, speed 50%, lookahead) and 4.5 dB at the default ±6 dB;
    phrase-start error 2.5 dB mean with lookahead, 9.8 dB without; up to 1.0% of a core at 48 kHz; latency 30 ms
    with lookahead, none without. All on a synthetic vocal.
- Not measured: any comparison with FabFilter Pro-DS, Waves Vocal Rider, Antinode Clear or Extract:Dialogue. The
  site does not compare.
- None exist yet: testimonials, customer logos, press, user counts, paid pricing. Never fabricate any of them.

## Product Principles

1. Show the mechanism, not adjectives: every claim is one the engineering can back.
2. Free and open is a feature stated plainly, not a disclaimer.
3. Respect the engineer's time: formats, platforms and the download are never more than one step away.

## Accessibility & Inclusion

WCAG 2.2 AA. The plug-ins already support keyboard control and screen readers, so the site must too.
