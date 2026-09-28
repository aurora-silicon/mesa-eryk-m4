# Completed evidence offload

The host offload tool now accepts UTC timestamp cutoffs as well as whole dates.
It selects only dated, non-symlink directories with a final numeric guard
returncode. Incomplete/fault directories without that receipt stay on target.
A dry-run manifest is available. Each destination has its own selected-file
manifest, preserving previous archival selections.

Validated by a read-only selection then checksum-verified offload of390 completed
M4 experiments older than20260928T105500. Whole-file rsync transfer checksums
precede removal; local original evidence can seed the archive through hardlinks,
without inplace writes. Current and incomplete evidence is retained. Sources,
application captures and normal desktop libraries are not selected.

Archive: build/driver-roadmap-20260928/offloaded-m4-completed-1055. The separate
legacy shader-cache archive preserves only files older than10:30UTC; private
benchmark caches are untouched. Legacy-cache archiving completed:174,707 files,575,248,631 content bytes, with
rsync checksums verified before removal. The workstation retains the full manifest
and cache contents under build/driver-roadmap-20260928.
