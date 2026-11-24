#!/usr/bin/env bash
# link_env.sh - attach our PlatformIO env to the unmodified MeshCore submodule.
# Borrowed from Faro (radeksixi/sixi-tdeck-plus, Apache-2.0), scripts/link_env.sh.
#
# Creates external/MeshCore/platformio.local.ini -> ../../envs/scooby.ini
# Upstream's platformio.ini loads platformio.local.ini via extra_configs and git-ignores it,
# so the submodule stays clean. Idempotent. Refuses to overwrite a regular file.
set -euo pipefail
ROOT="$(cd "${1:-$(dirname "$0")/..}" && pwd)"
SUB="$ROOT/external/MeshCore"
TARGET_REL="../../envs/scooby.ini"
LINK="$SUB/platformio.local.ini"

[ -e "$SUB/platformio.ini" ] || { echo "FAIL: $SUB not initialised (git submodule update --init)"; exit 1; }
grep -q "platformio.local.ini" "$SUB/platformio.ini" || { echo "FAIL: upstream no longer loads platformio.local.ini"; exit 1; }

if [ -L "$LINK" ]; then
  [ "$(readlink "$LINK")" = "$TARGET_REL" ] && { echo "OK: link already in place"; exit 0; }
  rm "$LINK"
elif [ -e "$LINK" ]; then
  echo "FAIL: $LINK is a regular file - move it away"; exit 1
fi
ln -s "$TARGET_REL" "$LINK"
echo "OK: $LINK -> $TARGET_REL"
