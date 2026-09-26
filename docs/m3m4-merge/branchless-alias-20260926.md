# Safe buffer alias lookup without a leaf branch

An absent buffer-alias leaf means identity mapping. Select the permanent zero
page and offset zero for its metadata read instead of branching around that
read. Preserve the original fallback for addresses outside the application VA
window. The real leaf can be32KiB while the zero page is16KiB; both its pointer
and its entry offset must be selected safely. Metadata provenance recognizes
this selection without treating loaded application addresses as metadata.

Validation:306 compiler units,810 bounds checks,352 sparse byte/vector masks
with both dynamic and specialized VA geometry, and existing alias/provenance
checks pass. Forty CPU-exact strict sparse buffer configurations pass under
watchdog/thermal guards on both M3 and M4. M4 evidence:
20260926T123556449866Z-dx12-branchless-alias-sparse.

Unchanged M3 Satisfactory DX12 gameplay capture,4K output,30 measured frames:
check123617712276 passes the existing4/255 in at most32pixels image budget.
Warm timings123646825145,123708016811,123729206901:
3.629437, 3.644865, 3.626824FPS; median3.629437FPS,
versus3.507932FPS previously. No capture,quality,clock or image-budget change.
This is capture regression evidence,not complete Vulkan conformance.

Candidate library SHA256:
5ba2858079318c60c6a5fdb90a10fab906c8f5ab331e0341f49267b0ff0639ac.
Full source/binaries,images,timings and guards are retained in the local-only
gpu-replay-bench provenance/satisfactory-dx12-optimization-20260926 directory.
Normal desktop Mesa is unchanged.
