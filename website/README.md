# altvane.com

The Altvane website: a home page and one page per plug-in, built with [Astro](https://astro.build) as static files.

```console
cd website
npm install
npm run dev       # http://localhost:4321
npm run build     # static site in dist/
```

- `src/data/products.ts` holds every product fact on the site. Each one comes from `products/<code>/README.md`,
  `CHANGELOG.md` and `.github/release-matrix.json`, so update them together.
- `src/lib/dsp.ts` and `src/lib/figures.ts` compute the figures: the EQ arcs come from RBJ biquad responses, the
  Dialogue bars from three stems that sum to the input, the Compressor columns from a soft-knee gain computer,
  and the Limiter's waveform from a lookahead peak limiter that never lets a column past the ceiling. The
  same code draws the static SVG at build time and redraws it live in the browser.
- The design system is recorded in `DESIGN.md` at the repository root.
