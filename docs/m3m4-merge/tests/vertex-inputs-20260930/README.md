# Vertex/fragment input regressions

Pinned CTS 1.4.6.2 catalog selection with original global indices. Unsupported
cases remain in the selection. No skips or results renumber coverage.

From the workspace root, with a staged Mesa prefix and the existing M3 guards:

```sh
python3 tools/cts-regression.py --machine m3 \
  --cases mesa/m3m4-merged/docs/m3m4-merge/tests/vertex-inputs-20260930/cases.txt \
  --prefix /opt/asahi-m3m4-f7b0d4dce0a --batch-size 48 \
  --output build/vertex-inputs-FRESH --env AGX_MESA_DEBUG=nopromote
```

The qualified pre-default candidate explicitly enabled vertex inputs and Ioto
templates; the final G15S policy enables both automatically. `summary.json`
identifies the candidate actually exercised by the retained full selection.
The runtime wrapper arms the 60-second watchdog and thermal/progress guards.
