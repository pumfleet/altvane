# Changelog

## 0.1.0

First release of Altvane Dialogue, for macOS 11 or newer (Apple silicon and Intel) and Windows x64.

### Separation

- Voice, ambience and voice reverb, each with a level from Off to +12 dB; the stems always add up to the input
- DPDFNet speech estimation (Eco: 2-layer, High: 8-layer), statically linked ONNX Runtime, no files at run time
- Voice reverb split from the speech estimate and the residual with a late-reverb model
- Six-point sensitivity curve per stem
- Linked (mid) or per-channel stereo; mono and stereo buses; 44.1–192 kHz through an exact 48 kHz round trip
- Fixed reported latency; offline renders wait for the model, real-time frames that would be late play unprocessed
- Quality and stereo-mode changes cross-fade once the new model state is warm

### Interface

- Spectrum that stacks what each stem keeps under the input's outline; solo and Removed auditions
- Sensitivity curves edited on the spectrum, with host parameter menus per node
- Hero knobs in stem colours; quality, stereo mode, latency, output and bypass in the footer; output meter
- Presets with a factory set, A/B compare, undo/redo, settings shared with Altvane EQ
