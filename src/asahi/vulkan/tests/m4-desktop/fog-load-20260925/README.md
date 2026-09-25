# Satisfactory native Vulkan local-fog compatibility

FactoryGame 1.2.4.0 / UE5.6 requests color DONT_CARE on render pass 50065,
then uses ONE/SRC_ALPHA (color), ZERO/SRC_ALPHA (alpha) blending in draw
901901. The original full application recording contains the identical render
pass at call 551177; this is not a trim or decoder alteration. Discarding the
scene color leaves only the fog shader's output and makes the factory dark.

Expose the existing Mesa vk_dont_care_as_load compatibility option, default
false, and enable it for native Vulkan application name FactoryGame. Apply it
at dynamic-rendering entry, including emulated legacy render passes, without
modifying the caller's structures. CLEAR and NONE remain unchanged. This is an
application workaround, not a Vulkan conformance correction or shader rewrite.

M4 guarded checks on candidate DSO ac6623530dcd4f044235c597a569c87da95cf2cafd82d2427df4f908c698c21d:
- Two-pass blend: default LOAD, application DONT_CARE, and application CLEAR
  all have exact 32x32 RGBA8 output. Sources and log retained here. Generate
  using tools/vulkan-fragment-probe.py --preclear ... --blend-load ... .
- Original native Vulkan menu snapshot, unchanged shaders: 31-frame replay
  completes and the reviewed final image restores factory lighting and fog.
  Evidence 20260925T102517051980Z-satisfactory-vulkan-fog-load-menu, 85 thermal
  samples, peak106C, verified 60-second watchdog. No timing score.
- Bit-exact 65536-byte captured fog texel data at offset851968 ruled out typed
  float fetch corruption before identifying the incorrect attachment load op.

Install the matching drirc defaults as well as the DSO. The isolated validation
prefix sets DRIRC_CONFIGDIR to its share/drirc.d; using old installed defaults
correctly leaves DONT_CARE discarded. No M3 hardware execution. Live application
validation is still pending at this checkpoint. Full captures and images stay
in the local-only benchmark repository.
