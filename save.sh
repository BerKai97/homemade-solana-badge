#!/bin/sh
# The firmware work in a solana-os checkout back into this repo:
#   ./save.sh [dir]         (default: ../solana-os)
# Rewrites firmware/solana-os.patch from its changes to upstream files and
# copies the new folders back.
set -e
here=$(cd "$(dirname "$0")" && pwd)
dir=${1:-$here/../solana-os}
COMMIT=edad4d91afeb8c8523c40ae4f49c5d0c879a8b9f
# Upstream files changed, plus new files that live among upstream's own.
NEW="components/bsp/Kconfig components/bsp/bsp_sd_flash.c components/bsp/bsp_touch.c
     main/core/os_touch.c main/core/os_touch.h main/core/touchcal.c
     emulator/platform/bsp_sd_flash_emu.c emulator/platform/bsp_touch_emu.c
     partitions-diy.csv sdkconfig.diy"
{
    git -C "$dir" diff "$COMMIT"
    for f in $NEW; do
        (cd "$dir" && git diff --no-index /dev/null "$f" || true)
    done
} > "$here/firmware/solana-os.patch"
for d in sol wallet kb img; do
    rm -rf "$here/firmware/main/$d"
    cp -R "$dir/main/$d" "$here/firmware/main/$d"
done
cp "$dir/main/lua/lua_sol.c" "$here/firmware/main/lua/"
cp "$dir/emulator/harness/index.html" "$here/firmware/emulator/harness/"
echo "saved from $dir"
