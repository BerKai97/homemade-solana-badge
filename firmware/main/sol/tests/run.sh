#!/usr/bin/env bash
# Host test of the Pump app's Solana code: main/sol/tests/run.sh (needs gcc + OpenSSL headers).
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"; main="$here/../.."; shim="$main/../emulator/shim"
out="${TMPDIR:-/tmp}/pump_test"
gcc -std=gnu11 -Wall -Wno-deprecated-declarations -O1 -I"$main" -I"$here" -I"$shim" \
    "$here/pump_test.c" "$here/sha_shim.c" "$main/sol/sol.c" "$here/pump_ref.c" "$main/sol/vendor/tweetnacl.c" \
    -lcrypto -o "$out"
"$out"
