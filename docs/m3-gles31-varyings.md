# Zink varying capacity on M3 (2026-09-22)

Zink advertised 14 varying vectors (56 components), failing the ES3 minimum
of 15 vectors / 60 components. Honeykrisp exposes 116 vertex-output
components, but Zink divided the vector limit by two to reserve duplicate
transform-feedback exports.

Zink commit `1abd507049b68a9ecd51c30a9090f52a2cbcdfe8` removed those duplicate
exports: `update_so_info` inlines capture into the original output variables.
The reservation remained. Remove it, use the minimum of vertex-output and
fragment-input capacity, and retain Zink's existing ceiling of 16 vectors.
No Honeykrisp hardware limit or Vulkan capability was raised.

## Validation

Native M3 Zink/Honeykrisp, boot `1c5cdaba-93d8-4344-b10d-8a5e7c75ba9b`.
The standalone `tools/m3-gpu/gl-varying-limit.c` probe verifies all 16 live
vec4 varyings while rasterizing and capturing the same 64 components with
transform feedback. Every one of 256 RGBA pixels and 192 feedback components
matches the CPU reference. The renderer explicitly identifies Apple M3 Pro.
Evidence: `build/m3-gles31-20260922/triage-varyings/limit-probe`.

The unchanged pinned ES31 CTS session 2 selection covers all implementation
limits, varying linkage, varying-array indexing and transform feedback:
1175 Pass, 333 NotSupported, zero Fail/Crash/Timeout.
The original two varying-limit cases pass. Unsupported cases report their
own implementation-limit exclusions and are not counted as passes.

Every run uses a separate 45-second M3 guard, 60-second watchdog and five
PMP temperature channels sampled every 0.5 seconds, with the 95 C stop.
No GPU fault or reboot was observed. This is focused regression coverage,
not a full conformance result.

Evidence below is relative to `build/m3-gles31-20260922/triage-varyings`:

- `00/run`: {'Pass': 80}, peak 47.00 C; results SHA256 `cceedc2e2c9527939c3046bcd8d47ec3c2a9567c9f78ac8d5c6174e28f0ed8f9`.
- `01/run`: {'Pass': 80}, peak 47.00 C; results SHA256 `59d59b36d24a0dcc462e69cfb54e62970a1c1ac4dbd498e4b07b4a92c98e2f19`.
- `02/run`: {'Pass': 56, 'NotSupported': 24}, peak 48.75 C; results SHA256 `d8213cc84f1dae0a3dca45942bb9f06cbd1bc1e548b8624189b808fb099945ca`.
- `03/run`: {'Pass': 56, 'NotSupported': 24}, peak 46.50 C; results SHA256 `e38ac8d9f10f220abad0c3d0d272bec2fb11aea260f6c216b3f690f83eb15918`.
- `04/run`: {'Pass': 56, 'NotSupported': 24}, peak 47.25 C; results SHA256 `9bf878d3e5a5b2afb04aea5c22498ed18c556d11ce4b236d355d239a6f6afa9c`.
- `05/run`: {'Pass': 80}, peak 48.00 C; results SHA256 `e391af5eec0e90c89ee2177885f13e6cfecd3c7c7336083037491ec885bf0009`.
- `06/run`: {'Pass': 80}, peak 47.00 C; results SHA256 `e74292267aa9b193df6b8f3330cf14330ce85c260857a71f727396b22c60154a`.
- `07/run`: {'Pass': 36, 'NotSupported': 44}, peak 48.75 C; results SHA256 `22c7ce7381bf8a8117b3751e4e3eaea1c0b828a1884c92ccece170d16e795be7`.
- `08/run`: {'NotSupported': 56, 'Pass': 24}, peak 46.75 C; results SHA256 `dffb84ca0b927d71621199ab255d4a41f6600df9d99b803b26f3756ee397a43d`.
- `09/run`: {'NotSupported': 35, 'Pass': 45}, peak 48.50 C; results SHA256 `cac5d0e2310f406690663328bcdd94bb87dae3c993a11ff4d02c875ae082f8dc`.
- `10/run`: {'Pass': 80}, peak 47.75 C; results SHA256 `28d31fd8e348e62d5b20e76187078dedb6f7f7377deff3d04dc5dc995a837561`.
- `11/run`: {'Pass': 80}, peak 48.00 C; results SHA256 `ee63aae616f5be8b9fc6e61825008e7bbcedbd5ed48fbf85da39871f9259d318`.
- `12/run`: {'Pass': 50, 'NotSupported': 30}, peak 49.50 C; results SHA256 `6003a90be2d59a15208754f78169f09dbd21c499e16955b2889cead7f3fac69c`.
- `13/run`: {'NotSupported': 41, 'Pass': 39}, peak 49.25 C; results SHA256 `823f4c8e01cc2a4b877b6443e9e293b9937c93dc3924eb85654a2893dd51efbe`.
- `14/run`: {'NotSupported': 19, 'Pass': 61}, peak 49.50 C; results SHA256 `151942a6a232ee4dba388c57ce3d780b28f41f21b10b918fcd475db36e4949b0`.
- `15/run`: {'Pass': 62, 'NotSupported': 18}, peak 50.75 C; results SHA256 `941e7a5fe0de540ae9c325984d7c0de7d400ae51853619b32bae01719de3b4c6`.
- `16/run`: {'Pass': 62, 'NotSupported': 18}, peak 51.50 C; results SHA256 `93f6bde5e35171819bac16a8bca08e95adadc3bb887546fcb4c0b544330817a2`.
- `17/run`: {'Pass': 80}, peak 50.00 C; results SHA256 `34a85090820eeb54848ae18a738c9bbe5bdf2b5fed55b87cdd745957c511bc6c`.
- `18/run`: {'Pass': 68}, peak 49.50 C; results SHA256 `94bfd5dac4b0205c687eeabd906b8fd15f99909212f0ecd85f900deb0c8ac709`.
