# Compiler debug cache identities

`AGX_MESA_DEBUG=wait` and `noopt` previously used the same pipeline UUID and disk
cache namespace as normal compilation. This silently reused normal shader code
in diagnostic runs, or diagnostic shader code after returning to normal mode.
Code-generation compiler flags now occupy reserved high bits in the shared
compiler-flags key used by both identities. Logging and validation-only flags
are excluded. The change applies across device generations.

Cross-built the ARM64 Vulkan library. On M4, the guarded probe queries the actual
Vulkan pipelineCacheUUID in separate processes: normal, wait and noopt were
identical before and are distinct after; logging-only shaders still equals
normal. Retained before/after JSON includes watchdog and thermal evidence. No
M3 execution or claim of rendering correctness is made by this identity test.

Copy probe.py to the target and invoke it through gpu-experiment.py with a
40-second guard, arguments {output} and the isolated Mesa prefix. The prefix
must provide asahi.json and lib/. Diagnostic tested DSO SHA-256:
a4b60aafbd3653c2363ddcf257279940b3e6be38e143e862fd5dc1fe67c8c1ed.

No game shaders, proprietary assets or application traces are included here.
