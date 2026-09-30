# M3 vertex uniform reuse and bounded texture-loop optimization

Native operand lowering creates repeated reads of immutable uniforms after
ordinary CSE. Enable the existing late CSE pass for G15S application vertex
shaders through an explicit compiler key. Constant programs and internal
helpers retain their policy. Other GPUs retain their default behavior.

Early sampled-loop preservation prevents generic sampler expansion from
multiplying compiler work. After ordinary descriptor specialization, reconsider
only sampled loops with an exact bound of at most four iterations, a loop-analysis
cost of at most 128 instructions, and no containing or contained loop. Large,
unknown-bound and nested loops retain the preservation hint. NIR's normal
unrolling heuristic still decides whether an eligible loop should expand.
No sampled result, graphics setting, API capability or floating-point rule is
changed. Overrides `AGX_NATIVE_VERTEX_UNIFORM_CSE=false` and
`HK_NATIVE_SHORT_SAMPLE_LOOPS=false` participate in shader/pipeline cache keys.

## Measurements and qualification

The first unchanged Minecraft Vulkan paired replay gives 97.8697 FPS control
versus 100.7650 FPS with vertex CSE (+2.96%). A subsequent same-binary pair gives
100.4577 FPS with CSE versus 103.4889 FPS with CSE plus bounded loop release
(+3.02%). These separate comparisons must not be combined into a claimed final
same-session percentage. All first/middle/last reference frames match exactly.
These are GPU replay scores, not live Minecraft/Java simulation FPS.

Retained benchmark diagnostics: `m3-native-schedule-paired`,
`m3-short-loops-paired`, `m3-native-motion-paired`, `m3-short-loop-tiles-paired`.
Additional fragment preambles, general code-motion overrides, graphics load
scheduling and smaller tile budgets did not establish a useful additional gain;
none is enabled by this change. Experimental commits remain on
`experiment/m3-native-scheduling-20260930`.

The default candidate passed 1429 targeted CTS cases: 965 Pass / 464 NotSupported,
no failures or guard errors. See `tests/bounded-sample-loops-20260930`.
Host loop tests cover successful short-loop unrolling and retention of large,
dynamic and nested loops. Descriptor, Ioto program and backend optimizer tests
pass. The broad `agx3_graphics_compile` fixture has pre-existing binary/rejection
expectation failures: rebuilding unchanged HEAD produced byte-identical error
output. This is not a claim that every host test passes. Logs are retained under
`build/minecraft-vertex-cse`.
