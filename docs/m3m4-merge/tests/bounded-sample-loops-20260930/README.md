# Bounded sampled-loop and vertex uniform regression coverage

1429 unique cases from the frozen Vulkan CTS 1.4.6.2 catalog, retaining global
indices. This extends the previous 1196-case vertex selection with 128 GLSL
loop cases, all 9 GLSL/graphicsfuzz texture-loop cases and 96 filtering cases.
Result: **965 Pass / 464 NotSupported / zero other outcomes**, all guards healthy.
Candidate Vulkan SHA256: `41b0d1d2fa1360ad8ac1a29acc7671e006aae54f2f3173a6367f8d50df3d6fcb`.
Defaults on G15S, `AGX_MESA_DEBUG=nopromote`. Not a full conformance claim.

From the workspace root, after staging a candidate:

```sh
python3 tools/cts-regression.py --machine m3 \
  --cases mesa/m3m4-merged/docs/m3m4-merge/tests/bounded-sample-loops-20260930/cases.txt \
  --prefix /opt/CHOSEN_PREFIX --batch-size 48 \
  --output build/cts-bounded-loops-FRESH --env AGX_MESA_DEBUG=nopromote
```

Receipts: `build/minecraft-vertex-cse/cts-bounded-loops-20260930`.
The first attempt used an already occupied remote batch basename. The guard
refused to start; no test executed. Its stale fetched outcomes are excluded.
The reusable runner now hashes the full local evidence path into its remote
name and refuses an existing remote directory before copying or fetching data.
The runner is retained in `../../tools/cts-regression.py`; copy it to the
workspace `tools/` directory before invoking it.
