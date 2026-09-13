#!/usr/bin/env bash
# Waits for e5.95.exe to exit, then installs the freshly built plugin.
# The .fne is loaded into the IDE process, so it can only be replaced while the
# IDE is closed - and it must be replaced before the IDE is reopened, otherwise
# the next test runs the previous build.
set -u

SRC="e:/A模块目录/易语言UI开源/jade混合版/bin/Release/JadeHybrid.fne"
DST="D:/易语言/lib/JadeHybrid.fne"
DEADLINE=$(( $(date +%s) + 3600 ))

# Both executables in D:/易语言 load the plugin and so hold the .fne open, even
# though only e5.95.exe is a host the memory bridge can work in. Waiting on just
# one of them copies over a file the other still has mapped.
# Two IMAGENAME filters are ANDed by tasklist, which no process can satisfy, so
# the whole list is matched instead.
ide_running() {
    tasklist //NH 2>/dev/null | grep -cEi "^(e|e5\.95)\.exe[[:space:]]"
}

while [ "$(ide_running)" != "0" ]; do
    if [ "$(date +%s)" -ge "$DEADLINE" ]; then
        echo "TIMEOUT: e5.95.exe still running after 60 minutes; nothing deployed"
        exit 2
    fi
    sleep 5
done

sleep 2
STAMP=$(date +%Y%m%d_%H%M%S)
BAK="$DST.before_host_check_$STAMP.bak"
cp "$DST" "$BAK" || { echo "FAILED to back up $DST"; exit 1; }
cp "$SRC" "$DST" || { echo "FAILED to copy plugin"; exit 1; }

echo "DEPLOYED at $STAMP"
echo "backup: $BAK"
md5sum "$SRC" "$DST"
ls -la --time-style=+%Y%m%d_%H%M%S "$DST" "D:/易语言/lib/jadehook.dll"
