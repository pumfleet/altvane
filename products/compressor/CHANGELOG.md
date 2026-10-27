# Changelog

## Unreleased

First version of Altvane Compressor, based on the ZL Compressor engine (forked at 0.6.1, upstream commit `2f7b45c`).

### Engine integration

- Mono tracks with a stereo side chain now hear the mid of the side chain when listening to it. ZL Compressor output
  silence here unless a side-chain EQ band was soloed.
- Host bypass is honoured on mono tracks when the host processes in double precision.
- The processor no longer keeps pointers into the host's audio buffers between blocks.

### Compatibility

- Same parameters and state format as ZL Compressor: presets and sessions load unchanged.
- Installs alongside ZL Compressor (different plug-in codes and bundle identifier).

The upstream changelog is kept in [docs/UPSTREAM_CHANGELOG.md](docs/UPSTREAM_CHANGELOG.md).
