# GLES client-index draw unrolling (2026-09-22)

The M3 ES31 campaign stopped on four primitive-restart cases using
DrawRangeElements. The reference render without restart raised
GL_INVALID_OPERATION. A serial four-case run failed only the first case;
subsequent cases passed, so a grouped rerun could conceal the bug.

An isolated diagnostic build logs glBegin/glEnd invalid calls. Backtraces
resolve to _mesa_unmarshal_Begin, glthread_unmarshal_batch and glGetError.
`should_convert_to_begin_end` selected the client-index upload optimization
in GLES. Commit b5f388a22205 inverted the profile predicate when replacing
`ctx->API == API_OPENGL_COMPAT` with a helper call. Restore the positive
compatibility-profile test: only that profile supports immediate mode.
The ordinary threaded upload path remains enabled in GLES.

The final build removes all temporary logging/backtrace instrumentation.
Each of the four original failures passes in a fresh process, and the full
504-case GLES3 primitive-restart selection passes with four workers. Guards
complete without GPU faults or reboot. This is regression coverage, not full
conformance. Test binaries and expectations are unchanged.

All runs retain the 45-second M3 guard, 60-second watchdog and five-channel
0.5-second temperature monitoring with a 95 C stop. Baseline and diagnostic
records are under `build/m3-gles31-20260922/triage-primitive-restart`, including
`serial`, `backtrace`, and the uncommitted diagnostic patch.

## Passing evidence

- `build/m3-gles31-20260922/triage-primitive-restart/exact-0/run`: {'Pass': 1}, peak 50.00 C; results SHA256 `31033a74a57005a5068166fd64ffb071634d7d5b73fc4f6984da184c5c66997e`.
- `build/m3-gles31-20260922/triage-primitive-restart/exact-1/run`: {'Pass': 1}, peak 47.25 C; results SHA256 `c54b87b6231015ac27d979358f432516f81c00515efda60502ad20f893d559c4`.
- `build/m3-gles31-20260922/triage-primitive-restart/exact-2/run`: {'Pass': 1}, peak 47.50 C; results SHA256 `47f4621ffeb48d233ecd9cc5f2afedfb431e3c8e74d7571772aad6aeb0e0519a`.
- `build/m3-gles31-20260922/triage-primitive-restart/exact-3/run`: {'Pass': 1}, peak 48.00 C; results SHA256 `64f3ec0e2731ff2ec18faf472853c3105eff672d263f88a80bc3f2ad31676872`.
- `build/m3-gles31-20260922/triage-primitive-restart/neighbors-00/run`: {'Pass': 128}, peak 48.25 C; results SHA256 `56d0c66f27d07f1a3381116c5e6fd65c4861ade59e22ae23fee27b8b12e67cc3`.
- `build/m3-gles31-20260922/triage-primitive-restart/neighbors-01/run`: {'Pass': 128}, peak 49.50 C; results SHA256 `92787228b15bda06fcbfa679e7885804e9137848b985fe0adec93f591a7c551b`.
- `build/m3-gles31-20260922/triage-primitive-restart/neighbors-02/run`: {'Pass': 128}, peak 51.00 C; results SHA256 `0dc550d8fbe5dddadaed1f33843ed32e87731025b06d7165a17ac8ce4d476e53`.
- `build/m3-gles31-20260922/triage-primitive-restart/neighbors-03/run`: {'Pass': 120}, peak 50.00 C; results SHA256 `921581080b619a3bb2e1301da991fff0e274c418097b4830178e305920297354`.
