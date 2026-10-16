#!/usr/bin/env bash
# Translate the user's own Sonic and the Black Knight main.dol (from GameData/sys/main.dol)
# into C++ and emit the native build graph. No game data is stored in this repository.
set -euo pipefail
workspace=$(cd "$(dirname "$0")/.." && pwd)
project=$workspace/projects/satbk/recomp.yml
translator_dll=${TRANSLATOR_DLL:-$workspace/translator/src/Translator.Cli/bin/Release/net8.0/Translator.Cli.dll}
threads=${THREADS:-$(nproc 2>/dev/null || echo 4)}
generated=$workspace/generated
functions=$generated/functions
base_metadata=$generated/base_translation_output.json
base_manifest_dir=$workspace/build/base
shards=$generated/build_shards

translator() { dotnet "$translator_dll" "$@"; }

dol=$workspace/GameData/sys/main.dol
[[ -f "$dol" ]] || { echo "Missing $dol - run the extractor on your own disc image first." >&2; exit 1; }
expected=b49266a531c64305848f4feb67f927873d209f7e5bb81f593794ca5b96e65dee
actual=$(sha256sum "$dol" | cut -d' ' -f1)
[[ "$actual" == "$expected" ]] || { echo "main.dol is not the clean USA (RENE8P) build: $actual" >&2; exit 1; }

mkdir -p "$generated" "$base_manifest_dir" "$generated/rso"
# Link hbm_part.rso the way the game does (module @ 0x900022E0, .bss @ 0x91754BE0, far-call
# stubs @ 0x90000860) so its code can be translated statically. Built from the user's files.
python3 "$workspace/tools/satbk_rso_image.py" "$workspace/GameData/files/hbm_part.rso" \
    "$workspace/GameData/files/BLACKKNIGHT.sel" 900022E0 91754BE0 90000860 \
    "$generated/rso/hbm_linked.bin" "$generated/rso/hbm_stubs.bin" "$generated/rso/hbm_starts.txt"
cat "$workspace/projects/satbk/MAP.txt" "$generated/rso/hbm_starts.txt" | sort -u -k1,1 > "$generated/function_map.txt"
translator translate-recursive 0x80006124 --project "$project" \
    --outdir "$functions" --output-metadata "$base_metadata" \
    --production-source-bundle "$generated/base_translation_sources.bin" \
    --no-function-files --prune-stale --threads "$threads"
# (no REL/mods for SatBK) translator emit-base-manifest --project "$project" --out "$base_manifest_dir" \
#    --functions-dir ...
translator generate-data-init --project "$project"
# The port loads main.dol from GameData at runtime; never embed game data in the executable.
rm -rf "$generated/data_sections_init.cpp" "$generated/data_sections_init_blobs.S" "$generated/data_sections_init_blobs"
translator emit-build-shards --project "$project" --base-metadata "$base_metadata" \
    --base-functions-dir "$functions" --native-source-dir "$workspace/runtime/src" --out "$shards"
echo "translation complete"
