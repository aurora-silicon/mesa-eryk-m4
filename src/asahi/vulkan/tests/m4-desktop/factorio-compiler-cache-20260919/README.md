# M4 compiler-option cache identity

The UBO/descriptor prefetch, ordinary-sampler and direct-variant switches
change compiled shader binaries, but were missing from both pipeline UUIDs
and the disk-cache driver flags. An existing entry could therefore silently
reuse the executable without the requested optimization. Include those four
switches plus HK_M4_VERSION_SAMPLES in the upper word of compiler flags;
the existing 32-bit AGX debug mask is unchanged. Other chips are unchanged.

The probe performs no GPU submissions. Run its Python wrapper under the normal
watchdog/thermal guard, with the C helper compiled and installed in /root.
It checks each option independently and verifies repeated base configuration
stability. 20260919T022456458478Z-m4-compiler-cache-baseline confirms all six
configurations had the identical UUID before the fix.
20260919T022523458447Z-m4-compiler-cache-separated passes with six distinct,
repeatable identities. This fixes option isolation, not an FPS claim. Existing
shader caches remain on disk; no application data or cache directories are
removed. Full CTS remains deferred.
