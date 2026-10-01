#!/bin/bash
# Copy a local build to the Move. Schwung's Tools menu picks it up.
set -e
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$(dirname "$SCRIPT_DIR")"
HOST="${MOVE_HOST:-ableton@move.local}"
DEST=/data/UserData/schwung/modules/tools/munchi-tape

[ -d dist/munchi-tape ] || { echo "Run ./scripts/build.sh first."; exit 1; }
ssh "$HOST" "mkdir -p $DEST"
# the card is large and rarely changes: copy it only when missing
if ssh "$HOST" "[ -f $DEST/card/jammi_a1.wav ]"; then
    scp dist/munchi-tape/module.json dist/munchi-tape/help.json \
        dist/munchi-tape/README.md dist/munchi-tape/LICENSE dist/munchi-tape/THIRD_PARTY.md dist/munchi-tape/MANUAL.md "$HOST:$DEST/"
else
    scp -r dist/munchi-tape/card dist/munchi-tape/module.json dist/munchi-tape/help.json \
        dist/munchi-tape/README.md dist/munchi-tape/LICENSE dist/munchi-tape/THIRD_PARTY.md dist/munchi-tape/MANUAL.md "$HOST:$DEST/"
fi
# upload beside, then rename: a running Munchi Tape holds the old binary open
# ("text file busy" on a plain copy) and keeps it until it exits
scp dist/munchi-tape/standalone "$HOST:$DEST/standalone.new"
scp scripts/boot-entry.sh "$HOST:$DEST/boot-entry.sh"
ssh "$HOST" "chmod +x $DEST/standalone.new $DEST/boot-entry.sh && mv -f $DEST/standalone.new $DEST/standalone"
echo "Installed to $DEST. Launch it from Schwung's Tools menu."
