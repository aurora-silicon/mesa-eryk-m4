# Lazy graphics link allocation errors

Lazy VS/FS variant admission now carries the linker's VkResult through to
vkEndCommandBuffer. Previously the lazy caller discarded the checked link
status and classified every failure as FEATURE_NOT_PRESENT. Eager shader
upload was already corrected separately. Invalid shader/part compilation
still reports FEATURE_NOT_PRESENT; host/device allocation and upload failures
retain their existing distinct link status.

Failure to allocate the variant key returns host OOM. Failed hash insertion
releases the key and linked BO/wrapper; failed shader-part cache insertion
releases its cloned key, binary and part. Neither publishes incomplete cache
entries. Cache hits preserve their existing stable pointer and dirty state.

ARM Vulkan build passes. tools/test-lazy-link-errors.py extracts actual lazy
VS/FS admission and command-error paths; injected part, key, linker and variant
cache failures preserve error identity and unwind owned allocations. It also
checks successful cached reuse. This host test stubs lower compiler/linker
operations and does not claim kernel pressure or hardware execution coverage.

Hardware candidate b970144c62036cd959257bdbd1052db50100ae82866026abbef71ba8f837017e
staged separately at /opt/asahi-m3m4-lazy-link-20260928. Active preamble CTS
continues using its unchanged allocation-20260928 binary. Hardware lazy-map
failure/retry validation is pending its next clean test checkpoint.

## M3 hardware qualification

Old binary returns FEATURE_NOT_PRESENT (-8) after deliberate 16-KiB mapping
failure in either the vertex or fragment lazy linker. Candidate returns
OUT_OF_HOST_MEMORY (-1) at both same boundaries. Failed command buffers are
reset without submission; same-device fresh recording subsequently renders
and checks all 4,096 fragment SSBO values. Both guards finish normally. The
new binary also passes 178 rendering/external-memory CTS cases (six NS).
Full kernel pressure and hash-table allocator injection on hardware remain
separate from this userspace mapping boundary. Host cache-insertion ownership
tests cover that branch. M4 hardware qualification remains pending its active
preamble campaign finishing.

Reproducer: compile tools/vulkan-lazy-map-failure.c beside the retained
lazy-map-shaders/{vertex,fragment}.h, linking Vulkan and dl. LD_PRELOAD the
asahi-map-failure.c shared object; ASAHI_TEST_MAP_SIZE=16384 and
ASAHI_TEST_MAP_MATCH=1 targets the VS mapping, 2 the FS mapping in this exact
probe. Default expected result is -1; setting ASAHI_TEST_EXPECTED_RESULT=-8
records the old misclassification without accepting it as fixed. Actual linker
logs identify the stage; ordinal alone is not proof on an arbitrary workload.
Run only inside the established GPU guard. The diagnostic wrapper retains
mismatches without deliberately rebooting a healthy GPU; its `matched` field,
not merely its exit code, establishes the expected outcome.
