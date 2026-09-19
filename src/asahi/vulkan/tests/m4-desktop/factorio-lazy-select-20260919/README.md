# Conditional evaluation experiment, rejected for default enablement

The generic NIR pass in the archived patch reconstructs an if around costly
exclusive ALU inputs of grouped selects. It never moves memory/texture,
derivatives or subgroup operations; external uses retain their producers.
Host tests cover external users, predicate dependencies, dependent selects and
short arms, with NIR dominance validation. All32 recorded compiler bundles
compile. Sprite187485c5 becomes7681243d, with16exclusive ALU ops behind its
material predicate. This is not a shader-specific substitution.

Guarded unchanged5frame replay144017 is pixel-exact and retains all ten
fragment query totals. WholeGPU6.146..6.199ms versus6.166..6.260ms in142944;
fragment sums4.369..4.395 versus4.368..4.448ms. The difference is too small
for a convincing improvement from one pair. This does not explain the
remaining bottleneck or reach5ms. Do not enable by default. Active compiler
source restored; private lazy-select ICD remains for reproducibility only.

Peak69.5C, watchdog60s and six thermal channels enabled. No live application
run is claimed for this compiler experiment. Normal installed Mesa remains
a02c681565b0632fc0185deb08fcfcccecd8d41c6b39757a866472977ccb7e08.
