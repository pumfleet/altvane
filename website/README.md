# altvane.co

The Altvane website: a home page and one page per plug-in, built with [Astro](https://astro.build) as static files.

```console
cd website
npm install
npm run dev       # http://localhost:4321, or $BERTH_PORT
npm run build     # static site in dist/, with sitemap-index.xml
```

- `src/data/products.ts` holds every product fact on the site. Each one comes from `products/<code>/README.md`,
  `CHANGELOG.md`, `docs/BENCHMARK.md` and `.github/release-matrix.json`, so update them together.
- A product's `version` is set only once its GitHub release exists (`gh release list`). With a version, its action
  says "Download 0.1.0" and links to the release; without one, it says "Build from source" and links to the build
  instructions in its README.
- `src/lib/dsp.ts` and `src/lib/figures.ts` compute the figures: the EQ arcs come from RBJ biquad responses, the
  Dialogue bars from three stems that sum to the input, the Compressor columns from a soft-knee gain computer,
  the Limiter's waveform from a lookahead peak limiter that never lets a column past the ceiling, the De-Esser's
  spectrum from its detection band, Vocal weighting, soft knee and shelf (`products/deesser/source/dsp`), and the
  Leveler's moves from its rider, hop by hop (`products/leveler/source/dsp`). The same code draws the static SVG at
  build time and redraws it live in the browser.
- `public/images/<code>-editor.png` are the editors' screenshots, copied from `products/<code>/docs/screenshot.png`
  (the Limiter's is a 2× render by `AltvaneLimiterSnapshot`). Each is also its page's social image. `og.png`, the home
  page's, is a 1200×630 render of the wordmark, the claim and the EQ figure, made in the dev server.
- The design system is recorded in `DESIGN.md` at the repository root.
