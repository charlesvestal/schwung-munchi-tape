#!/bin/bash
# Copy a local build to the Move. Schwung's Tools menu picks it up.
set -e
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$(dirname "$SCRIPT_DIR")"
HOST="${MOVE_HOST:-ableton@move.local}"
DEST=/data/UserData/schwung/modules/tools/munchi

[ -d dist/munchi ] || { echo "Run ./scripts/build.sh first."; exit 1; }
ssh "$HOST" "mkdir -p $DEST"
# the card is large and rarely changes: copy it only when missing
if ssh "$HOST" "[ -f $DEST/card/jammi_a1.wav ]"; then
    scp dist/munchi/module.json dist/munchi/help.json \
        dist/munchi/README.md dist/munchi/LICENSE dist/munchi/THIRD_PARTY.md "$HOST:$DEST/"
else
    scp -r dist/munchi/card dist/munchi/module.json dist/munchi/help.json \
        dist/munchi/README.md dist/munchi/LICENSE dist/munchi/THIRD_PARTY.md "$HOST:$DEST/"
fi
# upload beside, then rename: a running Munchi holds the old binary open
# ("text file busy" on a plain copy) and keeps it until it exits
scp dist/munchi/standalone "$HOST:$DEST/standalone.new"
ssh "$HOST" "chmod +x $DEST/standalone.new && mv -f $DEST/standalone.new $DEST/standalone"
echo "Installed to $DEST. Launch it from Schwung's Tools menu."
