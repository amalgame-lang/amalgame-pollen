#!/usr/bin/env bash
# Smoke test runner for amalgame-pollen.
#
# Usage : ./tests/run_tests.sh <amc-binary>
#
# Expects the package archive at build/linux-x86_64/libamalgame-pkg-Pollen.a
# to be freshly built (the CI workflow does this just before invoking
# this script). Compiles each tests/*.c against that archive + runs it.

set -euo pipefail

AMC="${1:-./amc}"
if [ ! -x "$AMC" ]; then
    echo "no amc binary at $AMC" >&2
    exit 1
fi

AMC_DIR="$(dirname "$(readlink -f "$AMC")")"
# amc tarball layout : <prefix>/bin/amc + <prefix>/share/amalgame/runtime/
RUNTIME_DIR="$AMC_DIR/../share/amalgame/runtime"
if [ ! -d "$RUNTIME_DIR" ]; then
    # local dev layout : <amc-source>/amc + <amc-source>/runtime/
    RUNTIME_DIR="$AMC_DIR/runtime"
fi
if [ ! -d "$RUNTIME_DIR" ]; then
    echo "couldn't locate amc runtime headers (looked under $AMC_DIR)" >&2
    exit 1
fi

ARCHIVE="$(pwd)/build/linux-x86_64/libamalgame-pkg-Pollen.a"
if [ ! -f "$ARCHIVE" ]; then
    echo "package archive missing : $ARCHIVE — run the build step first" >&2
    exit 1
fi

fail=0
for c in tests/*.c; do
    [ -f "$c" ] || continue
    name="$(basename "$c" .c)"
    out="/tmp/amalgame-pollen-test-$name"
    echo "== compiling $c"
    gcc -O2 -I"$RUNTIME_DIR" -I./runtime "$c" "$ARCHIVE" \
        -lgc -lpthread -o "$out"
    echo "== running $name"
    if ! "$out"; then
        echo "FAIL $name"
        fail=1
    fi
done

if [ "$fail" -ne 0 ]; then
    echo "some tests failed" >&2
    exit 1
fi
echo "all tests passed"
