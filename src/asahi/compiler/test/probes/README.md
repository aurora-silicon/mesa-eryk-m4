
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

G16G normalized16 (AIR-NT32023.917, 2026-09-13): g16g-rg16-unorm-eot,
g16g-rg16-snorm-eot and g16g-rgba16-unorm-eot retain BTNF inputs plus complete
68-byte output. Format7/6 with2/4 components uses IMGWBLK f29=4 (tail a895),
not the prior packed32/integer selectors. g16g-eevee-five-eot retains the
exact12:4/11:1/7:2/19:4/19:4 mixed builtin. g16g-rg16-fetch.metal produces
the fragment entry in its .hex (text-1020.bin+64), including LLDCOL W16 mask3.
These are offline compiler witnesses, not native hardware success. M4 tests
20260913T075653546595Z-eevee-norm16-eot and080248481675Z-eevee-norm16-fetch-blend
qualify clear/draw/reload and signed/unsigned RG16/RGBA16 blending in Mesa.

G16G indexed array write: `g16g-image-array-indexed.metal/.hex` retains the
native compiler output from build/eevee-native-array-indexed (text-7e0, PACK234,
IMGWCI262). Native IMGWCI f79=4 selects table-base uniform u4. Mesa's native
PBE heap is in u0, so its corresponding instruction clears f79. Hardware
20260913T082842,082934,082953,083412 passes exact four/sixteen-layer, bounds,
RGBA16F and 32x32 controls. A separate diagnostic 083510 mirrored the heap
from u0 into u4 and restored f79=4: exact PASS. That mirroring is not retained.
These probes qualify array image writes, not complete Eevee rendering.
