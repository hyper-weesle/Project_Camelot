#!/usr/bin/env bash
# prepare runtime -> (translate unless NO_TRANSLATE=1) -> incremental Linux build -> copy to test dir
set -euo pipefail
ws=$(cd "$(dirname "$0")/.." && pwd); cd "$ws"
./tools/satbk_prepare_runtime.sh > /tmp/prep.log
if [[ "${NO_TRANSLATE:-0}" != 1 ]]; then
  THREADS=2 ./tools/satbk_translate.sh > /tmp/trans.log 2>&1 || { tail -20 /tmp/trans.log; exit 1; }
fi
cmake -S runtime -B native-build > /tmp/cmake.log 2>&1
ninja -C native-build -j2 WiiCompiled > /tmp/ninja.log 2>&1 || { grep -E "error|FAILED|undefined|multiple" /tmp/ninja.log | head -30; exit 1; }
echo "built: $(grep -c Building /tmp/ninja.log || true) objects"
cp native-build/WiiCompiled /home/claude/satbk/test/
