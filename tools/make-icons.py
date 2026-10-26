#!/usr/bin/env python3
"""Draws app/icon.png (80x80), app/icon-large.png (130x130) and app/splash.png.

Run from the repo root: python3 tools/make-icons.py
The images are committed; rerun only to change the artwork.
"""
import math
from PIL import Image, ImageDraw, ImageFont

BG = (18, 20, 24, 255)
PETAL = (242, 160, 61, 255)
PETAL_DARK = (214, 120, 30, 255)
CENTER = (150, 70, 20, 255)


def flower(draw, cx, cy, r):
    # Two rings of petals, outer darker, then the center.
    for ring, (count, rr, pr, color, off) in enumerate(
            [(12, r * 0.62, r * 0.36, PETAL_DARK, 0.0), (10, r * 0.40, r * 0.33, PETAL, math.pi / 10)]):
        for i in range(count):
            a = off + 2 * math.pi * i / count
            x, y = cx + rr * math.cos(a), cy + rr * math.sin(a)
            draw.ellipse([x - pr, y - pr, x + pr, y + pr], fill=color)
    c = r * 0.26
    draw.ellipse([cx - c, cy - c, cx + c, cy + c], fill=CENTER)


def icon(size, path):
    s = size * 4  # supersample
    img = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    d.rounded_rectangle([0, 0, s - 1, s - 1], radius=s * 0.22, fill=BG)
    flower(d, s / 2, s / 2, s * 0.40)
    img.resize((size, size), Image.LANCZOS).save(path)


def splash(path, font_path):
    w, h = 1920, 1080
    img = Image.new("RGBA", (w * 2, h * 2), BG)
    d = ImageDraw.Draw(img)
    flower(d, w, h - 120, 210)
    font = ImageFont.truetype(font_path, 120)
    text = "Marigold"
    tw = d.textlength(text, font=font)
    d.text((w - tw / 2, h + 170), text, font=font, fill=(255, 255, 255, 235))
    img.resize((w, h), Image.LANCZOS).convert("RGB").save(path)


if __name__ == "__main__":
    icon(80, "app/icon.png")
    icon(130, "app/icon-large.png")
    splash("app/splash.png", "app/fonts/InterDisplay-Medium.ttf")
