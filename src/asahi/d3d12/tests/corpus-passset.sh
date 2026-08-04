#!/bin/bash
# Emit the sorted set of corpus shader names that reach "NIR->AGX OK".
#
# The COUNT alone hides regressions that trade one shader for another, so the
# unit of comparison is this SET:
#
#   passset.sh > after.txt
#   comm -23 before.txt after.txt     # regressions -- must be empty
#   comm -13 before.txt after.txt     # newly passing
#
# Usage: passset.sh [path-to-corpus] [path-to-dxil_compile]
set -u

HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
CORPUS=${1:-$HERE/corpus}
TOOL=${2:-$HERE/../../../../_build/src/asahi/d3d12/d3d12agx_dxil_compile}

if [ ! -d "$CORPUS" ]; then
   echo "corpus not found at $CORPUS -- see tests/README.md" >&2
   exit 2
fi

for f in "$CORPUS"/*.dxil; do
   if timeout 60 "$TOOL" "$f" 2>&1 | grep -q "NIR->AGX OK"; then
      basename "$f" .dxil
   fi
done | sort
