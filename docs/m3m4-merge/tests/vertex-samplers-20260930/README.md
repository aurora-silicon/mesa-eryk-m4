# Ordinary vertex texture regression selection

1196 unique cases from the pinned Vulkan CTS 1.4.6.2 catalog. This unions the
previous vertex-input selection with 128 evenly spaced vertex sampled-descriptor
cases; one overlap is retained once. Original global indices are preserved.
The candidate completed 789 Pass / 407 NotSupported, no failures or guard errors.
This is targeted regression coverage, not full Vulkan conformance.

Candidate: `/opt/asahi-m3-ordinary-vertex-04e4a0be1ec3`, Vulkan SHA256
`04e4a0be1ec3af176c6927a7dda070e0f770509163b3fa0cc5c8c4130dd9b2a0`.
Boot: `60c95f06-c505-4cd2-a71c-3f7db27516f6`.
`AGX_MESA_DEBUG=nopromote`; default G15S vertex sampler policy, no feature override.

From the workspace root after staging the chosen prefix:

```sh
python3 tools/cts-regression.py --machine m3 \
  --cases mesa/m3m4-merged/docs/m3m4-merge/tests/vertex-samplers-20260930/cases.txt \
  --prefix /opt/CHOSEN_PREFIX --batch-size 48 \
  --output build/vertex-samplers-FRESH --env AGX_MESA_DEBUG=nopromote
```

Each segment uses the M3 watchdog, thermal and completion-progress guards.
Full receipts remain under `build/minecraft-inputs-next/cts-production`.
