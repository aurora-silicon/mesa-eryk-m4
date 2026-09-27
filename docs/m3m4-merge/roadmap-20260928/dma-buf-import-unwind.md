# Failed dma-buf imports release their ownership

The native import path acquired a GEM handle, then returned NULL on invalid
size without closing that new handle. It also asserted on fd-duplication failure
and aborted on VA allocation or GPU binding failure. These are recoverable
resource failures, not reasons to terminate a compositor or application.

Initialize the new slot's handle and fd sentinel before any fallible work.
All partial-construction failures use the existing agx_bo_free cleanup while
holding bo_map_lock. This releases only the newly acquired VA, duplicated fd and
GEM handle, and clears the slot before close. The caller still owns its input
fd on failure; existing imported objects keep their references.

An offline extraction test runs the production import and free methods against
observable DRM/fd/VA mocks: failed PRIME import, zero/negative/unaligned size,
failed dup, failed VA allocation, failed bind, successful repeat import,
zero-reference revival and final cleanup. It checks resource counts, lock
release, exact slot reset and preservation of the caller's fd. This is not
hardware injection or qualification of virtio.

Both M3 and M4 run all 96 frozen dma-buf/opaque-fd external-memory catalog cases:
90 Pass, six NotSupported each. The frozen selection retains global indices.
The separately installed library SHA256 is
b177fd900a73041f630978dc3ff787f0596124a2e3b0a13e96297ccdc1b9d3a9.
