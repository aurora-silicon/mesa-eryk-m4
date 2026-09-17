# Steam sign-in on the M4 hardware path

Steam build 1788652215 renders its full sign-in window in bounded run r46.
The final local screenshot was visually checked; credentials were not entered.
CEF's GPU process maps libvulkan_asahi.so and our libgallium-26.1.4.so, with no
lavapipe mapping. No no-sandbox override or GPU-disable workaround was added.
CEF resource limits remain enforced. The screenshot includes a temporary sign-in
QR and is retained locally, not published in this source evidence.

FEX-2604 needed independent allocator changes: bounded VMA metadata, committed
rpmalloc pages rather than writable whole spans, and stack growth bounded by
RLIMIT_STACK rather than its entire ASLR gap. Local FEX commits 7fdf965/e14b060
and rpmalloc 3b25f8a are retained with portable Git bundles and patches under
_provenance/fex-data-limit in the workspace. 32,786 host allocator assertions and
20/20 graphics-free 4 GiB-limit thread probes pass; 5 GiB allocation is still
rejected. These are not Mesa patches and have not been submitted upstream.
Installed FEX SHA256: 0cea7f40d1f2f324e5057a8e63bc4b3969032f9ab61b19e5a06a20c65103670c.

The isolated headless display must share the desktop audio runtime. Otherwise
Steam can block on the synchronous wpctl settings query even when CEF is
healthy. The helper exposes the existing audio sockets to muvm; production
rendering and CEF launch options are unchanged. This is a headless sign-in
rendering qualification, not successful account login or a game test.

Mesa overlay r8 contains the imported-view fixes for both guest architectures.
SHA256: b769786c00c18065f9dafdfa3cf94103c28571b6f8775ca75d0ae2682111a11d.
Final GLX32 and GLX64 each validate 256 pixels; DXVK FL11_1 validates 4096 clear
pixels, 4096 triangle/interpolation pixels and 256 compute words. Guard peak
61.75 °C. The original report missed a PTY OSC prefix on the last success line;
review.json records the corrected pass without rewriting raw evidence.

The separate DCP D114 handler passed its real hourly callback on September 17:
four events accepted, followed by 18 correct physical-panel cursor redraws.
Kernel qualification evidence is in rtkit-probe/d114-evidence. This does not
extend the Steam qualification to account login or games.
