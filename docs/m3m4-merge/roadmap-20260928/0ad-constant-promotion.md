# Constant promotion control

On M4, both candidate modes match the three saved 4K 0 A.D. reference frames
exactly. Same lazy-link-fix Mesa b970144c6203 and timing-counter kernel:

| Mode | Median FPS | Three samples |
|---|---:|---|
| Preambles off, ordinary compiler defaults | 26.8914 | 27.0341, 26.8914, 26.6806 |
| Preambles on, ordinary compiler defaults | 37.8050 | 37.8050, 37.8303, 36.9938 |
| Preambles on, retained replay nopromote policy | 38.8190 | 38.2889, 38.8781, 38.8190 |

The preamble gain remains about 40.6% with ordinary compiler defaults in both
rows. Removing the retained replay's nopromote flag is a small slowdown here,
not an optimization; no benchmark default was changed. These are 30-frame
saved-replay scores, not live-game FPS. All clocks, temperatures, exact
binaries and policy values remain in the benchmark records. The improvement
is enabling an existing experimental shader path, not a claim that the new
allocation-error fix itself speeds up rendering.
