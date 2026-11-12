#!/bin/sh
# img_thumb on the host, against Pillow: main/img/tests/run.sh
set -e
cd "$(dirname "$0")/../.."
out=$(mktemp -d)
gcc -O2 -Wall -Wextra -Wno-unused-parameter -Wno-sign-compare -I. -o "$out/thumb_cli" \
    img/tests/thumb_cli.c img/thumb.c img/vendor/miniz_tinfl.c img/vendor/tjpgd.c
python3 img/tests/thumb_test.py "$out/thumb_cli"
rm -rf "$out"
