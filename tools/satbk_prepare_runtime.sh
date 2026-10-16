#!/usr/bin/env bash
# Rebuild the SatBK runtime sources from the pristine upstream WiiCompiled runtime:
# automatic MKW->SatBK address remap, the hand-written SatBK patches, and the SatBK-only
# sources in satbk/runtime_src. Only files whose content changed are rewritten, so an
# incremental build (and the translated shards' precompiled header) stays valid.
set -euo pipefail
ws=$(cd "$(dirname "$0")/.." && pwd)
cd "$ws"
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
git archive "${UPSTREAM_REF:-bd219e3}" runtime/src runtime/include | tar -x -C "$tmp"
python3 tools/satbk_remap.py "$tmp/runtime/src" "$tmp/runtime/include"
python3 tools/satbk_patches.py "$tmp/runtime"
mkdir -p "$tmp/runtime/src/satbk"
cp satbk/runtime_src/*.cpp "$tmp/runtime/src/satbk/"
rsync -rc --delete "$tmp/runtime/src/" runtime/src/
rsync -rc --delete "$tmp/runtime/include/" runtime/include/
echo "runtime prepared"
