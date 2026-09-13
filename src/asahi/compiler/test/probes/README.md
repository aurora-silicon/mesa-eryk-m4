
G16G stencil export witnesses (2026-09-13): `g16g-stencil-native.metal` and
`g16g-depth-stencil-native.metal` compiled using AIR-NT32023.917 targeting
applegpu_g16g. Corresponding `.hex` files contain the fragment entry at64,
without the constant preamble. Full text hashes: stencil
fec248c893682db90b118e23e6bd1213a8024c304d7b5f258242dee5622a1e54;
depth/stencil c453168cd649831a6807cc5b475aca713769acf77906dfc226ec12c80470b150.
Local manifests and original archives: build/eevee-native-fragment-stencil
and build/eevee-native-fragment-depth-stencil. These are offline compiler
witnesses, not native M4 execution captures. Stencil goes in the upper16 bits
of the coverage word. The stencil-only staging COPY has f13=0; combined
export has f13=1 after depth publication. LD_MD0.38 is8 bytes for both.
Linux guarded probes065254 and065644 pass all1024 depth/stencil/color pixels
for combined and stencil-only modes respectively. Allocation8 alone did not
fix the initial stencil-only511 missingpixels (065210 and065457 retained).
