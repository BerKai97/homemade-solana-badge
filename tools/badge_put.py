#!/usr/bin/env python3
"""Copy a Lua app folder onto a badge (docs/LUA_API.md "Console").

  tools/badge_put.py apps/pump                             # sends only what changed
  tools/badge_put.py apps/pump --force
  tools/badge_put.py apps/pump --add images/coin.png=~/Pictures/coin.png
  tools/badge_put.py apps/pump --serial /dev/ttyACM1       # this port, not the one found by USB ID
  tools/badge_put.py apps/pump --out emulator/harness/apps/pump.txt   # lua put lines for the emulator

code.bin (the app's Lua compiled like the store's) needs node and the firmware's compiler:
  podman run --rm -v "$PWD":/src:Z -w /src docker.io/emscripten/emsdk:4.0.10 \\
      tools/luac/build.sh tools/luac/out/luac.mjs          # in the solana-os checkout
"""
import argparse
import base64
import hashlib
import json
import os
import re
import shutil
import struct
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
LUAC = os.path.join(HERE, "..", "..", "solana-os", "tools", "luac", "out", "luac.mjs")    # in setup.sh's checkout

CHUNK = 96          # base64 characters per `lua put` line (the console takes up to 128)
MAX_FILE = 256 * 1024   # LUA_PKG_FILE_MAX
WAIT = 10.0         # seconds for a reply (the console stops reading while it writes flash)
SUM = re.compile(r"^([0-9a-f]{64}) (\d+) (\S+)\r?$", re.M)
ACK = r"recv: (\w+) ?([^\r\n]*)\r?\n|(usage: lua[^\r\n]*)\r?\n"


def sha(data):
    return hashlib.sha256(data).hexdigest()


def app_files(folder):
    files = []
    for root, _, names in os.walk(folder):
        for n in sorted(names):
            rel = os.path.relpath(os.path.join(root, n), folder).replace(os.sep, "/")
            if n.startswith(".") or rel.endswith((".md", ".txt")) or "/." in rel:
                continue
            files.append(rel)
    order = {"app.json": 0, "main.lua": 1}
    return sorted(files, key=lambda f: (order.get(f, 2), f))


# Compiles with the firmware's own Lua (tools/luac): node runs the module.
COMPILE_JS = r"""
import { readFileSync } from "fs";
const { luac, files } = JSON.parse(readFileSync(0, "utf8"));
const m = await (await import(luac)).default();
const label = m.UTF8ToString(m._luac_label());
const chunks = [];
for (const f of files) {
  const src = Buffer.from(f.src, "base64"), name = Buffer.from(f.name + "\0");
  const ps = m._malloc(src.length + 1), pn = m._malloc(name.length);
  m.HEAPU8.set(src, ps); m.HEAPU8.set(name, pn);
  const n = m._luac_compile(ps, src.length, pn);
  m._free(ps); m._free(pn);
  if (n < 0) { console.error(m.UTF8ToString(m._luac_error())); process.exit(1); }
  chunks.push(Buffer.from(m.HEAPU8.subarray(m._luac_out(), m._luac_out() + n)).toString("base64"));
}
console.log(JSON.stringify({ label, chunks }));
"""


def find_luac(given):
    p = given or os.environ.get("BADGE_LUAC") or LUAC
    return os.path.abspath(p) if os.path.exists(p) else None


def code_bin(luac, files):
    """code.bin (lua_code.c) for the app's .lua files, main.lua first, and the compiler's label."""
    lua = [(rel, data) for rel, data in files if rel.endswith(".lua")]
    lua.sort(key=lambda f: f[0] != "main.lua")
    req = {"luac": "file://" + luac,
           "files": [{"name": rel, "src": base64.b64encode(data).decode()} for rel, data in lua]}
    r = subprocess.run(["node", "--input-type=module", "-e", COMPILE_JS], input=json.dumps(req),
                       capture_output=True, text=True)
    if r.returncode:
        sys.exit("compiling: " + (r.stderr.strip() or "node failed"))
    out = json.loads(r.stdout)
    chunks = [base64.b64decode(c) for c in out["chunks"]]
    head = 24 + 72 * len(lua)
    body, entries = b"", b""
    for (rel, _), chunk in zip(lua, chunks):
        if len(rel) > 64:
            sys.exit(f"{rel}: paths in code.bin are at most 64 bytes")
        body += b"\0" * (-(head + len(body)) % 16)
        entries += rel.encode().ljust(64, b"\0") + struct.pack("<II", head + len(body), len(chunk))
        body += chunk
    label = out["label"]
    return b"SOLC" + label.encode().ljust(16, b"\0") + struct.pack("<I", len(lua)) + entries + body, label


def load(folder, extra=(), luac=None):
    """The app's id and its (name, bytes) in sending order: app.json first, code.bin last."""
    with open(os.path.join(folder, "app.json"), encoding="utf-8") as f:
        manifest = json.load(f)
    app_id = manifest["id"]
    sources = [(rel, os.path.join(folder, rel)) for rel in app_files(folder)]
    for spec in extra:
        rel, _, src = spec.partition("=")
        if not src or rel != rel.lower() or rel.startswith("/") or ".." in rel:
            sys.exit(f"--add {spec}: use name=path, the name lowercase (a-z 0-9 _ - .)")
        sources.append((rel, os.path.expanduser(src)))
    files = []
    for rel, src in sources:
        with open(src, "rb") as f:
            files.append((rel, f.read()))
    if luac:
        code, label = code_bin(luac, files)
        manifest["code"] = {"luac": label, "sha256": sha(code), "size": len(code)}
        files = [(rel, json.dumps(manifest, indent=2).encode() if rel == "app.json" else data)
                 for rel, data in files] + [("code.bin", code)]     # last: app.json or a .lua file drops it
        print(f"{app_id}: compiled, code.bin {len(code)} bytes ({label})", file=sys.stderr)
    for rel, data in files:
        if len(data) > MAX_FILE and rel != "code.bin":
            sys.exit(f"{rel}: {len(data)} bytes; the badge takes up to {MAX_FILE} per file")
    return app_id, files


def put_lines(app_id, files):
    lines = []
    for rel, data in files:
        b64 = base64.b64encode(data).decode()
        lines.append(f"lua put {app_id} {rel} begin")
        lines += [f"lua put {app_id} {rel} {b64[i:i + CHUNK]}" for i in range(0, len(b64), CHUNK)]
        lines.append(f"lua put {app_id} {rel} end")
    return lines


def installed(badge, app_id):
    """{name: (sha256, size)} of the app's files on the badge, or None if it can't say."""
    badge.send(f"lua sums {app_id}")
    out, m = badge.read_until(r"(?:sums: (\d+) files|usage:|sums: no)[^\n]*\n", WAIT)
    have = {rel: (h, int(n)) for h, n, rel in SUM.findall(out)}
    return have if m and m[1] and len(have) == int(m[1]) else None


def recv(badge, app_id, rel, data):
    """One file with `lua recv`, a block each time the badge asks. None once its SHA-256 checks out."""
    b64 = base64.b64encode(data)
    badge.send(f"lua recv {app_id} {rel} {len(data)} {sha(data)}")
    while True:
        _, m = badge.read_until(ACK, WAIT)
        if not m:
            return "no answer"
        if m[3] or m[1] not in ("ready", "more"):
            return m[3] or (None if m[1] == "ok" else m[2])
        n = int(m[2])
        badge.write(b64[:n])
        b64 = b64[n:]


def send_recv(badge, app_id, files):
    """How many of files arrived before `lua recv` turned out not to work here, or None on an error."""
    for i, (rel, data) in enumerate(files):
        why = recv(badge, app_id, rel, data)
        if why is None:
            print(f"{app_id}/{rel}: {len(data)} bytes")
        elif why == "timeout" or why.startswith("usage: lua ["):
            print(f"lua recv {'got no data' if why == 'timeout' else 'is unknown'} on this port: "
                  "sending lua put lines", file=sys.stderr)
            badge.send("")                          # ends the line an unread block became
            badge.read_for(1.0)
            return i
        else:
            print(f"{app_id}/{rel}: {why}", file=sys.stderr)
            return None
    return len(files)


def send_lines(badge, lines):
    for i, line in enumerate(lines):
        badge.send(line)
        out, echoed = badge.read_echo(line, WAIT)
        reply = out.replace(line, "", 1)
        if not echoed or any(w in reply for w in ("usage:", "bad base64", "limited to", "can't", "not a manifest",
                                                   "first")):
            print(f"\nline {i + 1} ({line.split(' ')[3]}): {reply.strip()[-400:] or 'no echo'}", file=sys.stderr)
            return False
        if line.endswith(" end"):
            print(reply.strip().splitlines()[-1] if reply.strip() else line)
        print(f"\r{i + 1}/{len(lines)}", end="", file=sys.stderr)
    print(file=sys.stderr)
    return True


def serial_send(port, app_id, files, force=False, lines=False):
    from badge_serial import open_badge
    badge = open_badge(port)
    badge.send("open home")                         # the console refuses while the app runs
    badge.read_for(0.5)
    have = None if force else installed(badge, app_id)
    if have is not None:
        send = {rel for rel, d in files if have.get(rel) != (sha(d), len(d))}
        if any(rel == "app.json" or rel.endswith(".lua") for rel in send):
            send.add("code.bin")                    # the badge drops it
        print("unchanged: " + (", ".join(rel for rel, _ in files if rel not in send) or "none"), file=sys.stderr)
        files = [(rel, d) for rel, d in files if rel in send]
    print("sending: " + (", ".join(rel for rel, _ in files) or "nothing"), file=sys.stderr)
    n = 0 if lines else send_recv(badge, app_id, files)
    ok = n is not None and (n == len(files) or send_lines(badge, put_lines(app_id, files[n:])))
    badge.close()
    return ok


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("folder")
    ap.add_argument("--serial", nargs="?", metavar="PORT",
                    help="the badge's port, e.g. /dev/ttyACM0 (default: found by its USB ID)")
    ap.add_argument("--out", help="write the console lines to this file instead")
    ap.add_argument("--add", action="append", default=[], metavar="NAME=PATH",
                    help="also copy PATH as the app's file NAME (repeatable)")
    ap.add_argument("--luac", help="the firmware's compiler module (tools/luac/out/luac.mjs)")
    ap.add_argument("--no-code", action="store_true", help="send the source only; the badge compiles it")
    ap.add_argument("--force", action="store_true", help="send every file, changed or not")
    ap.add_argument("--lines", action="store_true", help="send with lua put lines, not lua recv")
    a = ap.parse_args()
    luac = None
    if not a.no_code:
        luac = find_luac(a.luac)
        if not luac or not shutil.which("node"):
            if a.luac:
                sys.exit(f"--luac {a.luac}: not found, or no node")
            print("no compiler (tools/luac/out/luac.mjs) or node: the badge compiles the source, which "
                  "takes more of its RAM", file=sys.stderr)
            luac = None
    app_id, files = load(a.folder, a.add, luac)
    if a.out:
        lines = put_lines(app_id, files)
        os.makedirs(os.path.dirname(a.out) or ".", exist_ok=True)
        with open(a.out, "w") as f:
            f.write("\n".join(lines) + "\n")
        print(f"{app_id}: {len(lines)} lines -> {a.out}")
        return
    t = time.time()
    ok = serial_send(a.serial, app_id, files, a.force, a.lines)
    print(f"{app_id}: " + (f"copied in {time.time() - t:.0f} s; open it from Apps or run: lua run {app_id}"
                           if ok else "FAILED"))
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
