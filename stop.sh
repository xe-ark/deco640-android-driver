#!/usr/bin/env bash
set -euo pipefail

DIR="$(cd "$(dirname "$0")" && pwd)"
su -c "pkill -x deco640d" >/dev/null 2>&1 || true
echo "deco640d stopped."
