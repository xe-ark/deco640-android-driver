#!/usr/bin/env bash
set -euo pipefail

DIR="$(cd "$(dirname "$0")" && pwd)"
ROT="${1:-270}"

if [ ! -x "$DIR/deco640d" ]; then
    echo "Building deco640d..."
    make -C "$DIR"
fi

su -c "am force-stop com.ugee.xpeen" >/dev/null 2>&1 || true
su -c "pkill -x deco640d" >/dev/null 2>&1 || true
sleep 0.3

su -c "nohup $DIR/deco640d --rot $ROT > $DIR/deco640d.log 2>&1 &"
sleep 0.8

su -c "pgrep -af deco640d" || true
cat "$DIR/deco640d.log" || true
