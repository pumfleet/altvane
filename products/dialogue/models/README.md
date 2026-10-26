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

## Measurements (M5 Max, one thread, ONNX Runtime 1.28 CPU)

| Model | Per 10 ms hop | Share of one core, per channel |
|---|---|---|
| dpdfnet2 (Eco) | 0.87 ms | 8.7% |
| dpdfnet8 (High) | 2.34 ms | 23.4% |

GRU and Gemm are about 70% of the time, so a hand-written inference path (Accelerate) is the obvious later speed-up.

## ONNX Runtime

`scripts/build_onnxruntime.sh` builds a static, CPU-only ONNX Runtime with only the operators in
`required_operators.config`. Regenerate that file whenever a model changes, from ORT-format conversions of the models
(the runtime's graph optimisations add `FusedConv`):

```
python -m onnxruntime.tools.convert_onnx_models_to_ort --optimization_style Fixed --target_platform arm <dir with the .onnx files>
```

and copy the generated `required_operators.config` here.

## Replacing a model

Our own three-stem model (dry voice, voice reverb, ambience) is planned to replace DPDFNet behind the same
`dsp::SeparatorModel` interface. Training data must be licence-clean: see the plan in the repository history before
adding a dataset.
