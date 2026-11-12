#!/usr/bin/env python3
"""img_thumb against Pillow: PNGs of every colour type and depth, transparency,
baseline and progressive JPEG. Needs Pillow. Run by tests/run.sh."""
import os
import struct
import subprocess
import sys
import tempfile

from PIL import Image, ImageDraw

CLI, SIDE = sys.argv[1], 64


def picture(w, h):
    im = Image.new("RGB", (w, h))
    d = ImageDraw.Draw(im)
    for y in range(h):
        d.line([(0, y), (w, y)], fill=(255 * y // h, 80, 255 - 255 * y // h))
    d.ellipse([w // 4, h // 4, 3 * w // 4, 3 * h // 4], fill=(20, 241, 149))
    return im


def reference(im):
    im = im.convert("RGBA")
    w, h = im.size
    c = min(w, h)
    im = im.crop(((w - c) // 2, (h - c) // 2, (w - c) // 2 + c, (h - c) // 2 + c))
    bg = Image.new("RGBA", im.size, (0, 0, 0, 255))
    return Image.alpha_composite(bg, im).convert("RGB").resize((SIDE, SIDE), Image.BOX)


def read_bin(path):
    data = open(path, "rb").read()
    magic, cf, _, w, h, stride, _ = struct.unpack("<BBHHHHH", data[:12])
    assert (magic, cf, w, h, stride) == (0x19, 0x12, SIDE, SIDE, SIDE * 2), (magic, cf, w, h, stride)
    px = struct.unpack(f"<{w * h}H", data[12:])
    return [((v >> 11) * 255 // 31, (v >> 5 & 63) * 255 // 63, (v & 31) * 255 // 31) for v in px]


def diff(a, b):
    return sum(abs(x - y) for p, q in zip(a, b) for x, y in zip(p, q)) / (len(a) * 3)


def case(name, im, fmt, tol, expect=None, ref=None, **save):
    with tempfile.TemporaryDirectory() as d:
        src, dst = os.path.join(d, "in." + fmt), os.path.join(d, "out.bin")
        im.save(src, **save)
        r = subprocess.run([CLI, src, dst, str(SIDE)], capture_output=True, text=True)
        got = r.stdout.strip()
        if expect:
            ok = expect in got
            print(f"{'ok  ' if ok else 'FAIL'} {name}: {got}")
            return ok
        if got != "ok":
            print(f"FAIL {name}: {got}")
            return False
        e = diff(read_bin(dst), list(reference(ref or Image.open(src)).get_flattened_data()))
        print(f"{'ok  ' if e <= tol else 'FAIL'} {name}: mean error {e:.2f} (up to {tol})")
        return e <= tol


base = picture(300, 200)
alpha = picture(257, 311).convert("RGBA")
a = Image.new("L", alpha.size, 0)
ImageDraw.Draw(a).rectangle([40, 40, 200, 260], fill=255)
alpha.putalpha(a)
pal = base.convert("P", palette=Image.ADAPTIVE, colors=200)
pal_t = alpha.convert("P", palette=Image.ADAPTIVE, colors=100)
results = [
    case("png rgb8", base, "png", 5),
    case("png rgba8", alpha, "png", 5),
    case("png palette8", pal, "png", 5),
    case("png palette+tRNS", pal_t, "png", 6),
    case("png palette4", base.convert("P", palette=Image.ADAPTIVE, colors=16), "png", 5, bits=4),
    case("png gray8", base.convert("L"), "png", 5),
    case("png gray+alpha", alpha.convert("LA"), "png", 5),
    case("png gray1", base.convert("1"), "png", 14),
    case("png gray16", base.convert("I").point(lambda v: v * 257).convert("I;16"), "png", 5, ref=base.convert("L")),
    case("png big 2000x1500", picture(2000, 1500), "png", 5),
    case("png tall 120x900", picture(120, 900), "png", 5),
    case("png small 40x40", picture(40, 40), "png", 8),
    case("jpeg small 40x40", picture(40, 40), "jpg", 12, quality=95),
    case("jpeg baseline 640x480", picture(640, 480), "jpg", 10, quality=90),
    case("jpeg 4000x3000", picture(4000, 3000), "jpg", 10, quality=85),
    case("jpeg gray", picture(500, 500).convert("L"), "jpg", 10),
    case("jpeg progressive", picture(640, 480), "jpg", 0, expect="progressive", progressive=True),
    case("png too wide", picture(4096, 64).convert("RGBA"), "png", 0, expect="too wide"),
    case("gif", base, "gif", 0, expect="not a PNG or JPEG"),
]
print("all passed" if all(results) else "FAILED")
sys.exit(0 if all(results) else 1)
