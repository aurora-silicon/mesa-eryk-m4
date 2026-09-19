# Desktop contribution to Factorio rendering

`m4-desktop-shader-variants.py OUTPUT [--compression]` runs on J713 under
`gpu-experiment.py`. It retains the old desktop environment and verifies that
a restarted Hyprland inherits the requested flags, restoring it on failure.
Existing SDDM autologin and game/compositor visual settings are preserved.
The opt-in compression path is lossless, restricted by normal Mesa eligibility;
it does not change texture assets, image dimensions or quality settings.

- 20260919T015526899409Z-desktop-m4-shader-variants: enabled UBO prefetch,
  ordinary sampler executable variants and pooled Ioto templates.
- 20260919T015557135486Z-factorio-desktop-variants-menu: live 60-second menu
  completes, 39.57 FPS tail. No isolated shader-performance claim.
- 20260919T021817115586Z-desktop-lossless-compression: removed nocompress and
  enabled HK_M4_COMPRESSED_2D / HK_M4_COMPRESSED_FEEDBACK with allocation audit.
  User compositor confirms actual compressed 2560x1664 targets.
- 20260919T021843440365Z-factorio-compressed-compositor-menu: live 60-second
  menu passes, 35.65 FPS tail (27.68--39.84), peak 87.75 C. Final 19.1 seconds:
  1436 panel renders, fragment total 2.227 s (1.55 ms/pass), versus 2.28 ms/pass
  in the preceding uncompressed-desktop sample. Scenes differ, so game FPS is
  not a paired comparison. Full-panel fragment cost is lower; 60 FPS remains
  unmet. User autologin and normal graphical boot remain enabled.

Descriptor-prefetch revisit with the current BO cache, variants and compute
copies: 20260919T023240632287Z-factorio-descriptor-prefetch-current-frame is
pixel-identical. The 60-second live run
20260919T023347445835Z-factorio-descriptor-prefetch-current-menu completes at
29.83 FPS tail (14.84--39.95), peak 90.75 C. It contains more compute passes
and a different animated scene than the 42.98 FPS preceding run. No isolated
benefit is established; normal launcher keeps descriptor prefetch disabled.

Short repeated-frame comparisons after recovery both complete:
20260919T023948434311Z-factorio-fixed-eight-no-prefetch-recovered reports
1.906 seconds for the measured loop section, versus 1.800 seconds with
prefetch in 20260919T024104558291Z-factorio-fixed-eight-prefetch. Replayer FPS
counts the repeated frame only once and must not be read as application FPS.
Captured game targets are 2560x1664, unlike the current 2512x1552 window.
Full-size fragment averages are 1.972 versus 1.950 ms in the retained summary
intervals, with unequal command counts at interval boundaries. This is a small
difference and does not justify a claimed live-menu speedup.

### Integer-only native preamble revisited

`HK_M4_NATIVE_PREAMBLE=true` moves uniform integer arithmetic into the
native constant program after Ioto resource loading. Only 32-bit exports
are allowed; float arithmetic and memory loads stay in the main shader.
This compiler choice receives its own cache identity bit. Default is false.

The former rejected experiment predates executable BO cache normalization.
With that correction, `20260919T025718413433Z-factorio-integer-preamble-current-frame`
reproduces the retained 2560x1664 frame exactly (0 differing components).
`20260919T025804817644Z-factorio-integer-preamble-current-menu` completes a
60-second unchanged 2512x1552 menu run: tail mean 37.433 FPS, 37.16–37.75,
peak sensor 89.25 C. This does not establish a performance win over the
different scenes in preceding 38–43 FPS runs, so the option stays disabled.
Broader correctness and floating/native preamble support remain TODO.
