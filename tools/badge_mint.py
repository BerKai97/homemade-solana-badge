#!/usr/bin/env python3
"""Give the badge a mint keypair made on this PC, for its next pump.fun launch.

  solana-keygen grind --ends-with pump:1       # writes <address>.json, ending in "pump"
  tools/badge_mint.py <address>.json           # to the badge found by USB ID (or give its port after)
  tools/badge_mint.py <address>.json --print   # the console line, for the emulator page

The file is solana-keygen's JSON (64 numbers) or a base58 secret key. The badge
keeps it in NVS until a mainnet launch uses it (devnet launches leave it), or
until `wallet mint off`. Keep the file: it's your only copy once that's done.
"""
import argparse
import json
import sys

ALPHABET = "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz"


def b58encode(b):
    n = int.from_bytes(b, "big")
    out = ""
    while n:
        n, r = divmod(n, 58)
        out = ALPHABET[r] + out
    return "1" * (len(b) - len(b.lstrip(b"\0"))) + out


def b58decode(s):
    n = 0
    for c in s:
        n = n * 58 + ALPHABET.index(c)
    raw = n.to_bytes((n.bit_length() + 7) // 8, "big")
    return b"\0" * (len(s) - len(s.lstrip("1"))) + raw


def load(path):
    text = open(path).read().strip()
    if text.startswith("["):
        secret = bytes(json.loads(text))
    else:
        secret = b58decode(text)
    if len(secret) != 64:
        sys.exit(f"{path}: expected a 64-byte secret key, got {len(secret)} bytes")
    return secret


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("keyfile")
    ap.add_argument("port", nargs="?", help="the badge's serial port, e.g. /dev/ttyACM0 (default: found by its USB ID)")
    ap.add_argument("--print", action="store_true", help="print the console line instead of sending it")
    a = ap.parse_args()

    secret = load(a.keyfile)
    address = b58encode(secret[32:])
    line = "wallet mint import " + b58encode(secret)
    print(f"mint: {address}", file=sys.stderr)
    if a.print:
        print(line)
        return

    from badge_serial import open_badge
    badge = open_badge(a.port)
    try:
        badge.read_for(0.2)
        badge.send(line)
        reply = badge.read_for(1.5)
    finally:
        badge.close()
    said = [l.strip() for l in reply.splitlines() if l.strip().startswith(("mint:", "that ", "expected", "could not"))]
    print("\n".join(said) or "no answer from the badge (is the console on that port?)")
    if not any(address in l for l in said):
        sys.exit(1)


if __name__ == "__main__":
    main()
