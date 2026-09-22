# Fixed descriptor build: initial EGL/GLES2 validation

On September 22 the unchanged final descriptor-fixed release build completed
segments [0,9), global configuration/case indices [0,20954): complete EGL and
first GLES2 session. 19,287 Pass, 1,662 NotSupported, four QualityWarning and
one CompatibilityWarning; **zero Fail, Crash, Timeout or GPUTimeout**.
The five warnings have the same status as in the stopped pre-fix campaign.
The original crashing pair at global index 1116 now Passes.

Every guard reports pass, same boot throughout. Maximum recorded temperature
109 C; 95 C startup gate, 110 C kernel regulation target, provisional 120 C
emergency limit, 0.5 s telemetry and 60 s watchdog retained. X11 EGL on the
private 1000 Hz headless compositor; GL four workers, EGL one. Case bound 60 s,
segment workload bound 900 s. Desktop/autologin stayed active. After collection
only the normal Hyprland remained and the watchdog returned to armed/idle.

This is **partial** validation: 20,954/326,602 completed, 305,648 Unrun.
The controller stopped normally at its selected boundary. No full campaign
or hardware workload is running. No historical crash-review permissions were
used. The two earlier rare GPU incidents remain unresolved.

Full campaign: workspace `build/gles32-descriptor-fixed-20260922`.
Catalog exactly equals the stopped pre-fix catalog; original indices and
failures are preserved. To continue on the SAME installed binaries:

```sh
python3 tools/gl-cts-plan.py run \
  --campaign build/gles32-descriptor-fixed-20260922 \
  --first 9 --continue-batch-deadlines
```

Use a durable controller with a report on exit and supervise guarded segments.
Before execution verify no competing workload and the armed 60-second watchdog.
If changing installed drivers, freeze a new campaign; never mix build outcomes.
The source-only slot optimization `6a6f211bc51` has not been deployed and is
not covered by these hardware results.

Pinned release Gallium SHA256:
`fd777b3abb0a4e7068305b8d3b9472d82387f5f9ce683fc131b8e873e5a02c8f`.
Vulkan SHA256:
`98891f7b35a025eba6271b30e1f58beb5fc03a2a4c55e52b546eb56daf47075d`.
CTS SHA256:
`b1b9630265dca8269fc34a3a062f801c259022055a56641bcf4d3fbbbc2a5070`.
Plan SHA256:
`3b044959c9db458c6c1a7daec89e7b52fb7b9fb1475eb9039d002dc62740ee1f`.

`results.json.gz` retains every completed indexed outcome. `segments.json`
links full workspace experiment evidence; compact guard results are mirrored.
Earlier descriptor-race reproducer and focused ASan validation are in
`../zink-descriptor-race/` (commit f9fc08ea091).
