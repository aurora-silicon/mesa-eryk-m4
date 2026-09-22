# M4 native advanced blend qualification (2026-09-22)

Enable EXT_blend_operation_advanced, coherent operations and dynamic advanced
blend state on G16G. Existing native tile epilogs now pass all supported cases;
no application shader substitutions or framebuffer-fetch capability claims.

* Monolithic: 3,722 Pass / 930 NotSupported, 4,652 total.
* Fast-linked/pipeline libraries and linked/unlinked SPIR-V/binary shader
  objects: 22,332 Pass / 5,580 NotSupported, 27,912 total.
* Total: 26,054 Pass / 6,510 NotSupported, zero failures/crashes/timeouts.
  Every skip requests 16 color attachments, above the supported maximum of 8.

Tests cover blend operations, coherent overlap, independent attachments,
premultiplied/nonpremultiplied color and correlated overlap. Exact selections,
per-case original results and summaries are compressed alongside this report.
No whole-suite compliance claim is made.

Evidence: 20260922T051000177928Z-blend-vk-all-monolithic (peak 98 C),
20260922T052350356900Z-blend-vk-all-other (peak 108.5 C). Mandatory watchdog and
thermal guards remained active. Both batches completed within their bounds.
Monolithic Vulkan SHA256:
47ff9f6bec2ef981c5a63747c8875e7763b14aae51b22e82b337ad994ce04eeb.
Other variants (including enabled dynamic advanced blend state) SHA256:
bbb00dd244d3aaa0d26c136460478f8ff0dfb7f4d407af94b64c621b638da7a2.
