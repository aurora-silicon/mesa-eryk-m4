# Imported BGRA format-view readback

The imported BGRA EGL image used an RGBA texture view. Zink rendered to a
replacement attachment, but set unflushed_transient on that child instead of
the imported parent. CPU/PBO reads consequently returned the untouched
original allocation (all zeros). Set the flag on the parent. Also preserve
copy/sampling usage for single-sample replacement images; attachment-only
usage is appropriate only for transient multisample resolve attachments.

The original guarded reproducer fails all16384 channel checks per BGRA read.
The flag-only revision returns correct pixels but violates image usage VUIDs;
its original report missed mixed-case Validation Error. review.json corrects
that result and the runner now matches case-insensitively. The final revision
passes with Vulkan validation enabled. Eight configurations (linear/tiled,
1x/4x, ordinary/imported texture view) pass all262144 pixel comparisons via
CPU/PBO and RGBA/BGRA reads, peak60.25C. The watchdog and six-channel thermal
guard cover every run. ARM64, x86_64 and i686 Gallium builds pass.

Installed ARM64 Gallium SHA256:
23d63ba4581d0ee5dee50d20c44b644a8cd82d42d995752827a05a6750b2a73f.
This closes the separate imported-view finding in the parent README. Full
Steam login and games remain separate application validation targets.
