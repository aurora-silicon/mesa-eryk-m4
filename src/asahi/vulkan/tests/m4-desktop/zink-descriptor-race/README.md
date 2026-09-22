# Zink descriptor allocator ownership race

Fix: Mesa commit `2da009f46e1`. Concurrent contexts modified a shared screen
ralloc child list under separate locks or without a lock. Use separate layout
and pool-key allocator parents and serialize allocation plus cache insertion.
Direct graphics/compute push layout creation uses the same layout lock.

The included patch is an upstream candidate, not an upstream submission.
A copy is kept under workspace
`reference/mesa-snippets/zink-descriptor-ownership-20260922/`.

## Reproduce without hardware

Copy `zink-descriptor-race.{c,py}` into the workspace-root `tools/` directory.
The runner expects `mesa/m4-asahi` and the existing
`build/mesa-m4-asahi-host/compile_commands.json` plus built util/Vulkan-util
archives. From the workspace root:

```sh
python3 tools/zink-descriptor-race.py --output build/NEW_UNIQUE_EVIDENCE_DIR
```

The output directory must not exist. This compiles the live descriptor code
and ralloc with ASan, retains assertions, enables leak detection, and bounds
the process to 30 seconds. Eight threads mix cached and direct layout creation
with pool-key allocation, then destroy the screen. Vulkan layout calls are
stubbed; this checks host allocator ownership, not GPU behavior.

## Evidence and limits

- `host-before-b`: original code, ASan heap-use-after-free with the original
  narrower reproducer. Its exact old C source was not separately retained.
- `host-push-before`: expanded reproducer catches the incomplete cache-only
  fix (ASan heap-use-after-free). Direct push layout creation also needs locking.
- `host-final`: complete fix, expanded reproducer Pass and ten repeat Passes,
  assertions and leak checks enabled. Build commands and ASan logs retained.
- `host-handoff-verification`: independent continuation-session rerun of the
  committed fix and mirrored harness, Pass with assertions and leak checks.
- `sharing-final-asan`: final private ASan Gallium, 38/38 Pass on J713,
  including the original EGL multithread texture-sharing crash case.
  Guard verdict pass, runner 26.351 s, total 30.375 s, peak 84 C.
- `sharing-before-asan` and `textures-before-asan`: original hardware builds
  passed 38 and 339 cases. The race is nondeterministic on hardware; these
  are not controlled failing baselines.
- `sharing-after-asan`: the incomplete cache-only fix also passed 38/38.
  That focused pass did not establish complete lock coverage.

Final release Gallium SHA256:
`fd777b3abb0a4e7068305b8d3b9472d82387f5f9ce683fc131b8e873e5a02c8f`.
Final private ASan Gallium SHA256:
`3580cdd6917db3914dcccd899716b07ea388fc9725e4b8bfede2430dc05329fd`.
Vulkan was unchanged:
`98891f7b35a025eba6271b30e1f58beb5fc03a2a4c55e52b546eb56daf47075d`.

The final hardware command, case list, results, guard record and all-channel
thermal samples are retained here. Full logs remain in workspace
`_provenance/experiments/20260922T131801073368Z-gles-sharing-final-asan`.
Hardware used the private 1000 Hz headless compositor, X11 EGL and the normal
60-second watchdog/case bounds. ASan applied only to the CTS process;
the desktop compositor remained uninstrumented.

The original campaign in `../gles32-retest` stopped at 48,454/326,602 with
one Crash. Its failed result remains unchanged. The fixed-build campaign
`build/gles32-descriptor-fixed-20260922` retains the same catalog and indices.
Full-suite validation is incomplete, two earlier GPU incidents remain open,
and no performance gain has been measured from this correctness fix.
