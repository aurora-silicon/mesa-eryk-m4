# Triage of seed 20260922

Offline classification of all 76 NotSupported, nine Fail and one Crash from
the frozen run. `triage.json` maps every original index to its category and
first blocking message. No reruns or driver/test changes. Categories count
first blockers; fixing one can reveal another.

## Crash

697: confirmed Mesa SIGABRT in `hk_CmdBeginRendering`, hk_cmd_draw.c:1118.
The depth-format assertion excludes newly exposed X8_D24. Audit the following
format-dependent setup and emulated physical-depth path too; removing the
assertion alone does not establish correct rendering. The GPU, display and
watchdog stayed healthy; no kernel panic or GPU hang occurred.

## Nine failures

| Count | Indices | Diagnosis |
| ---: | --- | --- |
| 4 | 116, 388, 704, 977 | USCALED-to-float linear blit mismatches across 8/16/packed-10-bit sources and R16/R32-float destinations. Likely related numerical paths; driver versus CTS reference ownership remains unconfirmed. |
| 2 | 342, 424 | Tessellation/geometry gaps. 342 rejects HAL200 tessellation-evaluation program state with outputs=0; 424 cannot encode `st_vary #4` in the generated geometry-rasterization vertex shader. |
| 1 | 412 | Unsigned-integer cube gather: compiler rejects the cube texture operation. |
| 1 | 58 | Attachment-allocation render pass triggers an unsupported `block_image_store` in generated store code. Not memory exhaustion. |
| 1 | 970 | ETC2 punch-through-alpha sRGB image written via RGBA16_USCALED texel view: large image mismatch. Raw view writes, compressed interpretation and historical CTS reference need isolation. |

Four failures are directly localized to Mesa compiler/pipeline rejections.
The five pixel mismatches need a known-good reference comparison before
assigning their cause. Maximum red-channel blit errors are 0.125, 0.0374908,
1.59961 and 0.5, versus tolerances 0.005, 0.00392157, 0.001 and 0.005.
The texel-view case reports 115.846 versus 0.001. No result identifies a kernel
fault. Compiler captures and QPA/stdout remain in the original evidence paths.

## 76 unsupported

| Count | First blocker | Interpretation |
| ---: | --- | --- |
| 13 | SSCALED linear filtering | Missing signed-scaled filtering; some destinations have additional restrictions. |
| 8 | R4G4_UNORM_PACK8 | Three sampling, two blit-source, two blit-destination and one blending case. |
| 5 | Packed SNORM10 | Four blit sources and one destination. |
| 2 | Packed SSCALED10 stores | Two blit destinations. |
| 2 | Storage texel-buffer formats | R8G8_USCALED (VkFormat enum 18) and R16G16B16_SINT (enum 89). |
| 1 | R64_UINT image | Sampled-image query fails; distinct from shader FP64. |
| 19 | Single-plane disjoint requests | Eighteen copies and one packed-422 conversion request disjoint support on a single-plane format. Does not imply ordinary multi-plane YCbCr is broken. |
| 4 | RGB/RGBA YCbCr conversion | Conversion/chroma-location feature flags absent for non-subsampled R5G6B5, RGBA8, RGBA16 and R10X6G10X6B10X6A10X6. Ordinary sampling is separate. |
| 12 | Sparse resources | Seven buffer-creation, three sparse texture shader and two sparse-resource cases. Ten queue lookup errors specifically require sparse binding, not normal graphics/compute queues. |
| 3 | shaderFloat64 | Optional double-precision shader support is false. |
| 3 | High multisample counts | 32x rasterization, 64x storage image and 64x sampled image exceed the advertised M4 1x/2x/4x counts. |
| 2 | Lazy attachment memory | Both QPA logs explicitly request lazy allocation. Missing memory type, not exhausted RAM. |
| 2 | Impossible negative CTS cases | All compatible formats already support the feature; no negative extended-usage case exists. Do not remove support to force a run. |

Totals: 31 format skips, 23 single-plane YCbCr/disjoint options, 12 sparse cases,
and ten other capability/test limitations. No Windows or Wayland setup errors.
Numeric format names and sparse queue requirements were checked against the
matching CTS source; lazy allocation was confirmed in both saved QPA logs.

## Suggested order

Fix the X8_D24 assertion and validate its depth path first. Investigate the four
related scaled-to-float blits together against a known-good renderer. Replay
the captured store/cube-gather/point-mode compiler failures, then fix the
remaining tessellation state and compressed-view mismatch. For optional format
expansion, signed-scaled filtering is the largest bucket, followed by R4G4 and
packed signed formats. Sparse support is a larger feature project. Keep all
fixes/retests separate from this frozen first-attempt report.
