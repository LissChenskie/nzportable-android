#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Draws the command lists written by tests/test_touch_menu.c (TM_DUMP_DIR) as PNG pictures, so the touch settings screen can be
looked at without a device. The real game font is an 8 px bitmap font; here a monospace font stands in for it at the same cell
size (7 px per scale step), so layout, clipping and overflow are shown faithfully, glyph shapes are not.
usage: render_touch_menu.py <dump dir> <out dir> [name ...]"""
import sys, os, glob
from PIL import Image, ImageDraw, ImageFont

def font(size):
    for path in ("/usr/share/fonts/truetype/dejavu/DejaVuSansMono-Bold.ttf", "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf"):
        if os.path.exists(path):
            return ImageFont.truetype(path, size)
    return ImageFont.load_default()

def render(path, out):
    lines = open(path).read().splitlines()
    w, h = map(int, lines[0].split()[1:3])
    img = Image.new("RGB", (w, h), (0, 0, 0))
    over = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    draw = ImageDraw.Draw(img)
    for line in lines[1:]:
        if line.startswith("fill "):
            x, y, fw, fh, r, g, b, a = map(int, line.split()[1:])
            if a >= 255:
                draw.rectangle([x, y, x + fw - 1, y + fh - 1], fill=(r, g, b))
            else:
                layer = Image.new("RGBA", (fw, fh), (r, g, b, a))
                img.paste(layer, (x, y), layer)
        elif line.startswith("text "):
            head, text = line.split("|", 1)
            x, y, scale, tw, r, g, b = map(int, head.split()[1:])
            f = font(int(8 * scale * 1.25))
            for i, ch in enumerate(text):
                draw.text((x + i * 7 * scale, y - int(scale * 1.2)), ch, font=f, fill=(r, g, b))
    img.save(out)

if __name__ == "__main__":
    src, dst = sys.argv[1], sys.argv[2]
    os.makedirs(dst, exist_ok=True)
    names = sys.argv[3:]
    for p in sorted(glob.glob(os.path.join(src, "*.cmd"))):
        n = os.path.basename(p)[:-4]
        if names and n not in names:
            continue
        render(p, os.path.join(dst, n + ".png"))
        print("rendered", n)
