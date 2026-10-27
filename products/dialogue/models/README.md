# Altvane Dialogue models

The plug-in embeds these models. CMake downloads each one at a pinned revision, checks its SHA-256 and compiles it into
`altvane_dialogue_models` with `#embed`, so no model file is committed or needed at run time (`models.cmake`).

| Model | Used for | Source | Revision | SHA-256 | Licence |
|---|---|---|---|---|---|
| `dpdfnet2_48khz_hr.onnx` | Quality: Eco | [Ceva-IP/DPDFNet](https://huggingface.co/Ceva-IP/DPDFNet) `onnx/` | `dd6818d00f50c836fed43a6243ebe49116de5964` | `7f0575a5…291dc14b` | Apache-2.0 |
| `dpdfnet8_48khz_hr.onnx` | Quality: High | same | same | `7b3afbb2…5c43f5631` | Apache-2.0 |

DPDFNet (Ceva, 2025, [arXiv:2512.16420](https://arxiv.org/abs/2512.16420)) is a causal, full-band speech
enhancement model in the DeepFilterNet family. Its streaming ONNX export takes one STFT frame (960-point Vorbis
window, 480 hop, 48 kHz, unnormalised) and a flat recurrent state, and returns the speech estimate of the frame four
hops earlier (40 ms look-ahead). The state starts at zero except for two running normalisations, whose initial values
are in the model's metadata (`erb_norm_init`, `spec_norm_init`). Weights and code are Apache-2.0 (model card and
[repository](https://github.com/ceva-ip/DPDFNet)); see `licenses/DPDFNet_LICENSE.txt`.

## Running the network

The plug-in runs DPDFNet with its own code, not a general ONNX runtime:

- `source/dsp/onnx_file` reads the embedded ONNX file's protobuf directly: initializers, Constant nodes, each node's
  inputs and the metadata.
- `source/dsp/dpdfnet` is the streaming network, written out by hand after Ceva's reference
  (`onnx_model/dpdfnet_48khz_hr.py`). It takes each weight from the node that uses it in the graph, checks its shape,
  and checks that its own state adds up to the export's `state_size`; any other layout is refused at load.
- `source/dsp/kernels` does the matrix products: on Accelerate (`cblas_sgemm`, which runs on the matrix units) when
  many rows share a weight matrix or the matrix is large, otherwise a register-tiled loop the compiler vectorises.

Per 10 ms frame, High does about 67M multiply-adds and Eco about 21M. About 90% of that is in the dual-path blocks:
64-wide GRUs across the 40 + 48 frequency positions, then across time at each position. Every layer that applies one
weight matrix at all positions is a single matrix product. In Stereo and Mid/Side modes the two streams are stacked into
the same products, so each weight is read once per frame, not once per channel. What stays on the CPU is the
across-frequency recurrence, which is sequential: about 1,400 small steps per frame for High.

`tests/model_tests.cpp` checks the output against values from ONNX Runtime. Given identical frames the two agree to
about 130 dB, which is float rounding. It also checks that two streams run together equal separate runs, and that
the Accelerate and portable paths agree.

## Measurements

Before the change, ONNX Runtime 1.28 on an M5 Max, one thread:

| Model | Per 10 ms hop | Share of one core, per channel |
|---|---|---|
| dpdfnet2 (Eco) | 0.87 ms | 8.7% |
| dpdfnet8 (High) | 2.34 ms | 23.4% |

ONNX Runtime 1.31 (C++ API, the plug-in's session options) and this code side by side, on one core of a shared
x86-64 machine (AVX2, no BLAS, so the portable path only), best of 25 runs of 120 frames:

| Model | ONNX Runtime, per channel | Ours, one stream | Ours, two streams (Stereo, Mid/Side) |
|---|---|---|---|
| dpdfnet2 (Eco) | 1.03 ms | 0.74 ms | 1.35 ms (2.06 ms with ONNX Runtime) |
| dpdfnet8 (High) | 2.9 ms | 2.03 ms | 3.6 ms (5.8 ms with ONNX Runtime) |

The M5 Max figures with Accelerate are still to be measured (`AltvaneDialogueRender`, with and without
`--no-blas`; `docs/BENCHMARK.md`).

## Latency

The model sets most of the plug-in's latency: its 20 ms analysis frame and its 40 ms look-ahead (`conv_lookahead`
and `df_lookahead` of two frames each; the estimate of a frame arrives four hops later). That is 60 ms
(`kAlgorithmicLatency`, 2879 samples) before the engine adds anything, and no engine change can go below it.
Monitoring live needs a model without look-ahead: with both look-aheads at zero, the floor drops to the 20 ms frame,
and a shorter frame (10 ms with a 5 ms hop) lowers it further.

## Replacing a model

Our own three-stem model (dry voice, voice reverb, ambience) is planned to replace DPDFNet behind the same
`dsp::SeparatorModel` interface. Since the network is written out in C++, a new architecture needs its forward pass
written too, in the same way as `dsp/dpdfnet` (most of DPDFNet's layers can be reused), and checked against ONNX
Runtime as `tests/model_tests.cpp` does. A model that keeps DPDFNet's layout but has new weights drops in as it is.
Training data must be licence-clean: `training/README.md` lists the allowed datasets and how to set up the training
machine.
