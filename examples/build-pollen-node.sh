#!/usr/bin/env bash
# Build examples/pollen-node.am into a runnable binary, linked
# against the resolved amalgame-pollen package archive.
#
# amc emits C ; we gcc-link it ourselves (same pattern as Mosaic's
# build) against the package archive + the amc runtime + stdlib.
#
# Usage : ./examples/build-pollen-node.sh [output-path]
#   default output : ./examples/pollen-node

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="${1:-$ROOT/examples/pollen-node}"
SRC="$ROOT/examples/pollen-node.am"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

RT="$HOME/.local/share/amalgame/runtime"
LIB="$HOME/.local/share/amalgame/lib/libamalgame.a"

# Resolve the package (no-op if already cached) + locate its archive.
( cd "$WORK" && amc package add pollen >/dev/null 2>&1 || true )
PKG_DIR="$(ls -d "$HOME"/.amalgame/packages/github.com/amalgame-lang/amalgame-pollen/v*_*/ 2>/dev/null | sort -V | tail -1)"
PKG_DIR="${PKG_DIR%/}"
PKGA="$PKG_DIR/build/linux-x86_64/libamalgame-pkg-Pollen.a"

if [ ! -f "$PKGA" ]; then
    echo "build-pollen-node: package archive not found at $PKGA" >&2
    echo "  run 'amc package add pollen' first" >&2
    exit 1
fi

echo "→ package: $(basename "$PKG_DIR")"
echo "→ amc $SRC → C"
( cd "$WORK" && cp "$SRC" pollen-node.am && amc --quiet pollen-node.am -o "$WORK/pollen-node" >/dev/null )

echo "→ gcc link → $OUT"
gcc -O2 -I"$RT" -I"$PKG_DIR/runtime" \
    -Wno-int-conversion -Wno-incompatible-pointer-types \
    "$WORK/pollen-node.c" \
    -Wl,--start-group "$PKGA" "$LIB" -Wl,--end-group \
    -lgc -lm -lpthread \
    -o "$OUT"

echo "✓ built $OUT"
