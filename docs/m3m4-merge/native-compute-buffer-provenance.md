# Prove canonical compute descriptor addresses

The runtime direct-buffer tag avoids alias-table reads, but still leaves a
conditional page-walk program at every buffer access. When sparse binding and
descriptor buffers are disabled, every ordinary descriptor is built by our
descriptor-update path and names canonical backing. Capture/replay buffers may
still have a different public BDA; their descriptors also name canonical backing.

Expose that invariant with an explicit tag in compute NIR. Alias lowering
recognizes the tag through packed pointer components, 32-bit offsets and robust
zero-page selections and removes the unnecessary translation before backend
optimization. Arbitrary pointer loads and unconstrained 64-bit arithmetic stop
the proof. Raw BDA and sparse accesses retain their full translation. No access
bounds, sparse residency behavior, shader work or application settings change.

The enabled-device feature combination is part of the existing shader cache key.
Unknown feature sets take the conservative runtime-tag path. Static provenance
is enabled only for native compute, the path qualified against exact images.

Validation includes six host provenance forms (including BDA loaded from a
canonical buffer and an unknown 64-bit offset), both forms of the existing
208-case sparse mask suite, tagged/untagged load/store/atomic alias tests and all
296 backend compiler tests. Hardware evidence and repeated 30-frame timings are
retained in the private `gpu-replay-bench` repository. The final isolated prefix
is `/opt/asahi-m3m4-satisfactory-native-opt` on M3 and M4.

An all-stage experiment was faster but changed a small number of RGB pixels
identically on M3 and M4. A graphics-only experiment reproduced the difference;
compute-only images matched exactly. Graphics static provenance is therefore
not enabled and the image tolerance was not widened. Code-motion overrides did
not restore agreement and remain diagnostic-only. No conformance conclusion is
drawn from agreement with an earlier hardware image.
