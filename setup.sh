#!/bin/sh
# Solana OS with this repo's firmware in it, ready to build:
#   ./setup.sh [dir]        (default: ../solana-os)
# Clones solana-os into dir (unless it's there), checks out the commit
# firmware/solana-os.patch is made for, applies the patch (once) and copies
# firmware/ over. Run it again after pulling this repo; ./save.sh goes the
# other way.
set -e
here=$(cd "$(dirname "$0")" && pwd)
dir=${1:-$here/../solana-os}
UPSTREAM=https://github.com/solana-foundation/solana-os.git
COMMIT=edad4d91afeb8c8523c40ae4f49c5d0c879a8b9f

[ -d "$dir/.git" ] || git clone "$UPSTREAM" "$dir"
if [ "$(git -C "$dir" rev-parse HEAD)" != "$COMMIT" ]; then
    git -C "$dir" checkout -q "$COMMIT"
fi
if git -C "$dir" apply --reverse --check "$here/firmware/solana-os.patch" 2>/dev/null; then
    echo "patch: already applied"
else
    git -C "$dir" apply "$here/firmware/solana-os.patch"
    echo "patch: applied"
fi
cp -R "$here/firmware/main/." "$dir/main/"
cp -R "$here/firmware/emulator/." "$dir/emulator/"
echo "ready: $dir"
echo "  firmware:  cd $dir && idf.py build          (ESP-IDF 6.0.1)"
echo "  emulator:  README.md, Emulator"
