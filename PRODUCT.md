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

Altvane makes audio plug-ins: Altvane EQ (parametric and dynamic equalizer), Altvane Dialogue (neural dialogue
isolation), Altvane Compressor (compressor, upward compressor and expander) and Altvane Limiter (true-peak limiter
and loudness meter). The website introduces
the brand, gives each product its own page, and gets visitors to a free download.

## Positioning

The plug-ins are free and open source (AGPL-3.0), and their interfaces are engineered for precision. EQ's curves are
computed from the engine's real filter coefficients and tested against the processed audio, to within 0.01 dB. Dialogue's
stems always add up to the input, so at 0 dB it passes audio through unchanged. The interfaces stay minimal and
favour direct manipulation.

## Operating Context

Visitors are in a studio or an edit suite, and often reach the site from a forum, a GitHub link or a recommendation.
They need to know the formats (VST3, AU, LV2, Standalone; no AAX builds yet), the platforms (macOS, Windows, Linux;
Dialogue is macOS 11+ and Windows x64 for now) and the sample rates. They evaluate a plug-in by trying it in their host.

## Capabilities and Constraints

- The primary action is a free download from GitHub Releases (`github.com/pumfleet/altvane`). Each product also links
  to its source.
- Releases are tagged per product (`eq-vX.Y.Z`, `dialogue-vX.Y.Z`, `compressor-vX.Y.Z`, `limiter-vX.Y.Z`). EQ, Dialogue and Limiter are
  at 0.1.0. Every product is presented as available; one without a version yet says "Download" with no number.
- Release builds: macOS (arm64, x86-64; VST3, AU, Standalone), Windows (x64, arm64; VST3, LV2, Standalone) and Linux
  (x86-64, arm64; VST3, LV2, Standalone).
- Every product fact comes from `products/<code>/README.md` and `CHANGELOG.md`.

## Brand Commitments

- Name: Altvane. Products are named "Altvane EQ", "Altvane Dialogue", "Altvane Compressor" and "Altvane
  Limiter".
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

- Screenshots: `products/eq/docs/screenshot.png`, `products/dialogue/docs/screenshot.png`; the Limiter's is rendered by
  `AltvaneLimiterSnapshot` (`website/public/images/limiter-editor.png`, 2×). The Compressor mock is at
  `products/compressor/docs/design/mock.html`.
- Measured facts in the READMEs: EQ curve accuracy within 0.01 dB in the zero-latency modes; Dialogue CPU on an M5 Max
  of Eco 8% and High 22% of one core; latency of about 80 ms at 512-sample buffers.
- None exist yet: testimonials, customer logos, press, user counts, paid pricing. Never fabricate any of them.

## Product Principles

1. Show the mechanism, not adjectives: every claim is one the engineering can back.
2. Free and open is a feature stated plainly, not a disclaimer.
3. Respect the engineer's time: formats, platforms and the download are never more than one step away.

## Accessibility & Inclusion

WCAG 2.2 AA. The plug-ins already support keyboard control and screen readers, so the site must too.
