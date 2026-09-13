# G16G layered color EOT

Native J713/T8132 25G83 capture20260913T055829Z-layer-depth-128-layers4-color-capture-fwtrace
renders four128x128 RGBA32Float layers with exact quarter-valued colors.
Two262144-component readbacks pass. The adjacent84-byte shader is resource1
+0x2700 from its encoded snapshot. It uses the tile-grid Z SR, PACK_IMGWIR
layer followed by the tile byte offset, and IMGWBLK array mode.

AIR-NT32023.917 applegpu_g16g builtin type3/F16 input (native-eot-input.py
12:4 --array) produces the same stream except the typed-store selector.
UMD21f7b8..c0 chooses type2/3 according to render layer count; the live M4
color witness independently confirms the layered header and both packs.
This compiler input is not a claim that a foreign GPU executed the program.

Mesa uses this ABI for one unspilled, single-sample layered color target.
The compiler matches all semantic wrapper operations, including layer Z;
it refuses other operations. Layered MRT and MSAA remain separate gaps.
GPU correctness of the new Mesa EOT awaits the bounded Linux color probe.
