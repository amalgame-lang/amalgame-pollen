#!/usr/bin/env bash
# Build + run every tests/*.am smoke against the working tree.
#
# Usage : ./tests/run_am_tests.sh [amc-binary]
#
# No git tag needed : the working tree is staged in the amc package
# cache as a dev version (tag v0.0.0-dev, rev localdev), and a scratch
# project pins it in its amalgame.lock. Other dependencies (threading)
# come from packages-index as usual. A test fails if it prints a
# [FAIL] line, exits non-zero or runs longer than 60 s.
#
# Env : ONLY=<test name> runs one test ; KEEP=1 keeps the scratch
# project. On Windows (Git Bash), amc writes its intermediate files
# to <drive>:\tmp of the current directory : put the scratch project
# on the same drive as the repo, e.g. TMPDIR=/d/tmp/pollen-am.

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
AMC="${1:-$(command -v amc)}"
if [ ! -x "$AMC" ]; then
    echo "no amc binary (pass it as the first argument)" >&2
    exit 1
fi

DEV_TAG="v0.0.0-dev"
DEV_REV="localdev"
PKG_ROOT="$HOME/.amalgame/packages/github.com/amalgame-lang/amalgame-pollen"
DEV_DIR="$PKG_ROOT/${DEV_TAG}_${DEV_REV}"

# Stage the working tree. Drop amc's compiled-facade cache so the
# staged facade.am is what gets compiled.
rm -rf "$DEV_DIR" /tmp/amc-pkg-Pollen*
mkdir -p "$DEV_DIR"
cp "$ROOT/facade.am" "$ROOT/amalgame.toml" "$DEV_DIR/"
cp -r "$ROOT/runtime" "$DEV_DIR/runtime"

WORK="$(mktemp -d)"
[ -n "${KEEP:-}" ] || trap 'rm -rf "$WORK" "$DEV_DIR"' EXIT
[ -n "${KEEP:-}" ] && echo "work dir : $WORK"
cd "$WORK"
cp -r "$ROOT/examples" .
: > amalgame.toml
if ! "$AMC" package add threading@v0.1.0 >/dev/null 2>&1; then
    # Offline (no access to GitHub) : pin the copy already in the cache.
    cached=$(ls -d "$HOME/.amalgame/packages/github.com/amalgame-lang/amalgame-threading/v0.1.0_"* 2>/dev/null | head -1)
    if [ -z "$cached" ]; then
        echo "cannot install threading@v0.1.0 (offline, not in the cache)" >&2
        exit 1
    fi
    printf '[dependencies.threading]\ngit = "github.com/amalgame-lang/amalgame-threading"\ntag = "v0.1.0"\n' > amalgame.toml
    printf '[[package]]\nname = "threading"\ngit  = "github.com/amalgame-lang/amalgame-threading"\ntag  = "v0.1.0"\nrev  = "%s"\n' "${cached##*_}" > amalgame.lock
fi
cat >> amalgame.toml <<TOML

[dependencies.pollen]
git = "github.com/amalgame-lang/amalgame-pollen"
tag = "${DEV_TAG}"
TOML
cat >> amalgame.lock <<LOCK

[[package]]
name = "pollen"
git  = "github.com/amalgame-lang/amalgame-pollen"
tag  = "${DEV_TAG}"
rev  = "${DEV_REV}"
LOCK

fail=0
for src in "$ROOT"/tests/${ONLY:-*}.am; do
    name="$(basename "$src" .am)"
    cp "$src" .
    echo "== $name"
    if ! "$AMC" build "$name.am" -o "$name" > "$name.build.log" 2>&1; then
        grep -iE "error|undefined" "$name.build.log" | head -20 || true
        echo "FAIL $name (build)"; fail=1; continue
    fi
    if ! timeout 60 "./$name" > "$name.log" 2>&1; then
        tail -20 "$name.log"; echo "FAIL $name (exit)"; fail=1; continue
    fi
    # Two report styles : "[FAIL] what" lines, or a final "FAIL — n assertion(s)".
    if grep -qE '^\[FAIL\]|^FAIL' "$name.log"; then
        grep -E '^\[FAIL\]|^FAIL' "$name.log"; echo "FAIL $name"; fail=1; continue
    fi
    echo "ok   $name ($(grep -E '^\[PASS\]|^OK' "$name.log" | head -3 | tr '\n' ' '))"
done
exit $fail
