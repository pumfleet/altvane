# Altvane

Audio plug-ins by Altvane. This repository builds every Altvane product from one tree, with a shared build system,
shared third-party dependencies and a shared design system.

| Product | Directory | What it is |
|---|---|---|
| [Altvane EQ](products/eq/README.md) | `products/eq` | Parametric and dynamic equalizer, on the ZL Equalizer 2 engine |
| [Altvane Dialogue](products/dialogue/README.md) | `products/dialogue` | Neural dialogue isolation: voice, ambience and voice reverb levels (macOS 11+ and Windows x64 for now) |
| [Altvane Compressor](products/compressor/README.md) | `products/compressor` | Compressor, upward compressor and expander, on the ZL Compressor engine (in development) |
| [Altvane Leveler](products/leveler/README.md) | `products/leveler` | Automatic vocal rider with lookahead, voicing detection, a music side chain and automation write |

## Layout

```
CMakeLists.txt        superbuild: dependencies, libs/, then every product in ALTVANE_PRODUCTS
CMakePresets.json     mac-arm64-debug / mac-arm64-release
cmake/                AltvanePlugin.cmake (altvane_add_plugin), AltvaneVersion.cmake, AltvaneTests.cmake, ...
third_party/          JUCE, highway, nlopt (git submodules)
libs/ui/              altvane::ui: design tokens, fonts, look-and-feel, brand mark, widgets, editor shell, presets
products/<code>/      one directory per product: CMakeLists.txt, source/, tests/, tools/, packaging/, docs/
packaging/            installer scripts shared by every product
tools/icons/          renders each product's app icon and installer art
scripts/              upstream engine sync and checks
website/              altvane.com: the Astro site (see website/README.md)
```

## Build

Requirements: Clang (AppleClang 16+ or LLVM/Clang 17+), CMake 3.25+ and Ninja. On Linux also install:

```console
sudo apt install libasound2-dev libx11-dev libxcomposite-dev libxcursor-dev libxext-dev libxinerama-dev \
  libxrandr-dev libxrender-dev libfreetype-dev libfontconfig1-dev libxi-dev
```

```console
git clone --recursive https://github.com/pumfleet/altvane.git
cd altvane

# macOS (Apple silicon)
cmake --preset mac-arm64-release
cmake --build --preset mac-arm64-release

# any platform
cmake -B Builds -G Ninja -DCMAKE_BUILD_TYPE=Release -DALTVANE_FORMATS="VST3;LV2" -DALTVANE_SIMD_TARGET=AVX2 .
cmake --build Builds
```

Options:

- `ALTVANE_PRODUCTS`: products to build, by directory name, e.g. `eq`. By default, every product that builds on this
  platform (a product's `PLATFORMS` file, if it has one, lists them).
- `ALTVANE_FORMATS`: plug-in formats, e.g. `"VST3;AU;Standalone"`.
- `ALTVANE_SIMD_TARGET`: SIMD target, one of `SSE2`, `SSE4`, `AVX2` (x86-64) or `NEON` (arm64). Required.
- `ALTVANE_COPY_PLUGIN`: copy plug-ins to the system folders after building (on by default).
- `ALTVANE_BUILD_TESTS`: build each product's `<Product>Tests` executable and register it with CTest.
- `ALTVANE_BUILD_TOOLS`: build developer tools (each product's snapshot renderer, `AltvaneIcons`).
- Product options are listed in each product's README (e.g. `ALTVANE_EQ_BAND_NUM`).

```console
cmake --preset mac-arm64-debug -DALTVANE_BUILD_TESTS=ON -DALTVANE_BUILD_TOOLS=ON
cmake --build --preset mac-arm64-debug
ctest --test-dir build/mac-arm64-debug
```

## Releases

Each product is versioned and released on its own. Tag `<code>-vX.Y.Z` (e.g. `eq-v1.0.0`) and the `Release`
workflow builds, signs and packages that product for every platform it supports (macOS, Windows and Linux unless its `PLATFORMS` file says otherwise). Without a tag a product builds as the
default version in its `CMakeLists.txt`.

## Adding a product

1. Create `products/<code>/` with `source/` (a `PluginProcessor` with an `altvane::ui::Session` member, whose editor derives from `altvane::ui::EditorShell`), `tests/`,
   `packaging/` and a `CMakeLists.txt`:

   ```cmake
   altvane_product_version(<code> 0.1.0 version)
   altvane_add_plugin(Altvane<Name>
           PRODUCT_NAME "Altvane <Name>"
           CODE <code>
           PLUGIN_CODE Av..          # unique four characters, at least one upper-case
           VERSION ${version}
           VST3_CATEGORIES Dynamics
           AAX_CATEGORY AAX_ePlugInCategory_Dynamics)
   if (ALTVANE_BUILD_TESTS)
       altvane_add_tests(Altvane<Name> "${CMAKE_CURRENT_SOURCE_DIR}/tests")
   endif ()
   ```

   If the product only builds on some platforms, list them in `products/<code>/PLATFORMS` (one
   `<CMAKE_SYSTEM_NAME>-<arch>` per line, e.g. `Darwin-arm64`); the default product list and the release matrix
   (`.github/release-matrix.json`) leave the others out.

   `altvane_add_plugin` fills in the company, manufacturer code (`Altv`), bundle id (`com.altvane.<code>`), LV2 URI,
   formats and code signing settings, and creates `Altvane<Name>_Core` for tests and tools to link.
2. Generate icons and installer art: `tools/icons/make_icons.sh <AltvaneIcons> <code> <LABEL> [grid|knee|ride]` (the
   last argument picks the drawing behind the mark).
3. Add the product to the table above.

Code style follows the existing sources (`.clang-format`): snake_case files, trailing-underscore members, camelCase
methods. Product code lives in `altvane::<code>`; shared UI in `altvane::ui`.

## Products built on upstream engines

A product forked from another open-source plug-in (as Altvane EQ is from ZL Equalizer) vendors that engine
unmodified and records where it came from in `products/<code>/UPSTREAM`:

```console
scripts/check_upstream.sh eq          # the vendored engine matches the pinned upstream commit (CI runs this)
scripts/sync_upstream.sh eq <ref>     # replace the vendored engine with upstream <ref> and move the pin
```

`sync_upstream.sh` also lists what changed upstream outside the vendored directories (processor, state, CMake) so
those changes can be ported by hand.

To start a new product from an upstream engine, write `products/<code>/UPSTREAM` (copy `products/eq/UPSTREAM`, set
`REPO`, leave `COMMIT=""`) and run `scripts/sync_upstream.sh <code> <ref>`. It vendors the engine, registers its
submodules, lists the upstream files to adapt (processor, state, CMake), and warns if upstream pins a shared
`third_party/` dependency at a different commit.

## License

Altvane EQ and Altvane Compressor are licensed under the GNU Affero General Public License v3, as are ZL Equalizer
and ZL Compressor, which they are based on; see [LICENSE.md](LICENSE.md) and [NOTICE.md](NOTICE.md). Third-party licenses are in [licenses/](licenses).
