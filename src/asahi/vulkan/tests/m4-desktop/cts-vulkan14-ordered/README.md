# Ordered Vulkan 1.4 coverage

Pinned source: vulkan-cts-1.4.6.2, f6a29701220f34dd1407513bfe80d74ca7b392ce.
Frozen catalog: 3,206,030 Linux cases in ASCII lexicographic order, excluding
41,522 Windows/Android cases once. Capability skips stay indexed.

The included manifests identify the catalog and the first two prepared ranges:
`[0,100000)` and `[100000,200000)`. These are selection artifacts, not test
results. No GPU workload was launched while preparing this workflow.

The catalog and selected lists are under `build/cts-vulkan14-ordered/` in the
workspace. Recreate them with the commands in ../README-cts.md and verify the
recorded hashes. Catalog updates require a new catalog; never renumber an
ongoing campaign. Execute accounting ranges in bounded guarded subranges and
retain every original outcome and any separate fix/retry attempts.

Offline validation covered adjacent ranges, global indices, altered catalogs,
duplicate names including exclusions, bounds and existing outputs. Both real
100k lists passed the existing metadata validator and have no overlap. An
existing-collector fixture retained global indices and classified a 60-second
CTS Pass as Timeout. No hardware validation is claimed by those checks.
