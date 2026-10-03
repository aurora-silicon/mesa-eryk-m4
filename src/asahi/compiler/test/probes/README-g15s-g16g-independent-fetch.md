# Independent zero-LOD fragment texel fetches

Own source `g15s-g16g-independent-fetch.metal` was compiled offline on mac-m5
with AIR-NT32023.921 for both applegpu_g15s and applegpu_g16g:

```
python3 tools/native-metal-compile.py SOURCE --host root@mac-m5 --stage render \
  --function fs --target TARGET --output NEW_DIRECTORY
```

The complete 332-byte fragment text sections are identical, SHA256
`e88a8f76bad72558597cd60912d18acda3fe99d84455cafcf8340264d3dd9303`.
Main entry is 64; fetches at 0x96/0xa4/0xb2/0xc0 name coordinate banks
0/4/8/12. Their completion slots are 1/1/2/3; staging waits are 1/2/4/8.
`test_parallel_zero_lod_fetch` checks the full native instruction bytes and
canonical decode. This is encoding evidence, not a hardware/performance claim.

Source, archive, compiler versions and manifests remain under
`build/m3-fetch-bank-study-20261003/native-{g15s,g16g}`.
