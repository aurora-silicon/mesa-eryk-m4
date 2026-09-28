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
