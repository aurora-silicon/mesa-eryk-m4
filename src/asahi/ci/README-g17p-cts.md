# G17P staged Vulkan CTS bring-up

`run_g17p_staged_cts.py` promotes an A18 Pro/G17P system only after these
fail-fast gates: Asahi DRM render node, Honeykrisp device identity, loader/API
smoke, compute submission, and simple rendering submission. It derives small,
deterministic case lists from the installed Vulkan CTS must-pass list; it does
not advertise or force any Vulkan capability.

Preview tomorrow's exact run without opening a device:

```sh
python3 src/asahi/ci/run_g17p_staged_cts.py \
  --dry-run --mustpass /deqp-vk/mustpass/vk-main.txt.zst
```

Run the initial gates (one CTS process at a time by default):

```sh
sudo python3 src/asahi/ci/run_g17p_staged_cts.py \
  --mustpass /deqp-vk/mustpass/vk-main.txt.zst \
  --render-node /dev/dri/renderD128 --through triangle
```

The compute and triangle runs emit `A18_GPU_COMPUTE_PASS` and
`A18_GPU_TRIANGLE_PASS`; after inspecting those artifacts, rerun through
`expanded` for the CTS smoke marker. Full CTS is
deliberately double-gated and requires both `--through full` and
`--allow-full-cts`. Every run uses a fresh output directory and records the
plan, selected case lists, suite TOMLs, device/host properties, CTS output,
dmesg snapshots, detected kernel faults, timeouts, and SHA-256 artifact
inventory. Unreadable dmesg fails preflight unless external capture is
explicitly acknowledged with `--allow-unreadable-dmesg`.

The hardware ladder supplies `A18_GPU_ARTIFACT_DIR`; the runner uses it as the
output root and emits `summary.json`, `plan.json`, and `artifact-manifest.json`.
Successful smoke and full runs terminate with `A18_GPU_CTS_SMOKE_PASS` and
`A18_GPU_CTS_FULL_PASS`, respectively.
