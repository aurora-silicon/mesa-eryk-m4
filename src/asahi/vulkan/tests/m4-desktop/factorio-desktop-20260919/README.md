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
