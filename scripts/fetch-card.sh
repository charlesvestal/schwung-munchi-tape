#!/usr/bin/env bash
# Fetch the CHOMPI TAPE 2.0 factory card (MIT) at a pinned commit.
#
# The card is 84 samples, ~107 MB of WAV. It is not committed here: it is
# fetched from the upstream repository at a fixed commit, so a release is
# reproducible and this repo stays small. The "_double" files and firmware
# binary on the upstream card are left out -- Munchi derives the 2x read
# from the sample itself and writes "_double" files only for slots it saves.
set -euo pipefail

REPO="https://github.com/CHOMPI-Club/CHOMPI.git"
COMMIT="a73d732613da684e4de844619b690776f0f50ccf"
CARD_PATH="firmware/card-profiles/tape-2.0"

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(dirname "$SCRIPT_DIR")"
CACHE="$REPO_ROOT/.card-cache"
OUT="${1:-$REPO_ROOT/build/card}"

if [ ! -f "$CACHE/$COMMIT.ok" ]; then
    rm -rf "$CACHE/src"
    mkdir -p "$CACHE"
    git init -q "$CACHE/src"
    git -C "$CACHE/src" remote add origin "$REPO"
    git -C "$CACHE/src" config core.sparseCheckout true
    echo "$CARD_PATH/" > "$CACHE/src/.git/info/sparse-checkout"
    git -C "$CACHE/src" fetch -q --depth 1 origin "$COMMIT"
    git -C "$CACHE/src" checkout -q FETCH_HEAD
    touch "$CACHE/$COMMIT.ok"
fi

SRC="$CACHE/src/$CARD_PATH"
rm -rf "$OUT"
mkdir -p "$OUT"
n=0
for f in "$SRC"/*.wav; do
    case "$(basename "$f")" in
        *_double.wav) continue ;;
    esac
    cat "$f" > "$OUT/$(basename "$f")"
    n=$((n + 1))
done
cat "$SRC/presets.json" > "$OUT/presets.json"
cat "$SRC/options.json" > "$OUT/options.json"

if [ "$n" -ne 84 ]; then
    echo "fetch-card: expected 84 samples, got $n" >&2
    exit 1
fi
echo "fetch-card: $n samples -> $OUT"
