#!/usr/bin/env python3
"""Draws app/icon.png (80x80), app/icon-large.png (130x130) and app/splash.png;
with --tvos, the Apple TV app icon (two layers) and Top Shelf images in
src/platform/tvos/Assets.xcassets.

Run from the repo root: python3 tools/make-icons.py [--tvos]
The images are committed; rerun only to change the artwork.
"""
import json
import math
import os
import sys
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


# ---- Apple TV ----
# The app icon is a stack of layers the system tilts for parallax: an opaque
# back (the colour) and the flower in front. Top Shelf shows above the app
# when it is in the top row of the Home screen.

XCASSETS = "src/platform/tvos/Assets.xcassets"
INFO = {"author": "xcode", "version": 1}


def write_json(path, value):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w") as f:
        json.dump(value, f, indent=2)
        f.write("\n")


def back_layer(w, h):
    img = Image.new("RGBA", (w, h), BG)
    glow = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    d = ImageDraw.Draw(glow)
    r = h * 0.9
    for i in range(24):  # a soft warm light behind the flower
        k = 1 - i / 24
        a = int(26 * (1 - k))
        d.ellipse([w / 2 - r * k, h / 2 - r * k, w / 2 + r * k, h / 2 + r * k], fill=(120, 60, 18, a))
    return Image.alpha_composite(img, glow)


def front_layer(w, h):
    s = 4
    img = Image.new("RGBA", (w * s, h * s), (0, 0, 0, 0))
    flower(ImageDraw.Draw(img), w * s / 2, h * s / 2, h * s * 0.36)
    return img.resize((w, h), Image.LANCZOS)


def imagestack(name, sizes):
    """sizes: [(scale, w, h)]"""
    stack = f"{XCASSETS}/App Icon & Top Shelf Image.brandassets/{name}.imagestack"
    layers = [("Front", front_layer), ("Back", back_layer)]
    write_json(f"{stack}/Contents.json", {"info": INFO, "layers": [{"filename": f"{n}.imagestacklayer"} for n, _ in layers]})
    for layer, draw in layers:
        base = f"{stack}/{layer}.imagestacklayer"
        write_json(f"{base}/Contents.json", {"info": INFO})
        os.makedirs(f"{base}/Content.imageset", exist_ok=True)
        images = []
        for scale, w, h in sizes:
            fn = f"{layer.lower()}{'' if scale == '1x' else '@' + scale}.png"
            draw(w, h).save(f"{base}/Content.imageset/{fn}")
            images.append({"filename": fn, "idiom": "tv", "scale": scale})
        write_json(f"{base}/Content.imageset/Contents.json", {"images": images, "info": INFO})


def top_shelf(name, w1, h1, font_path):
    base = f"{XCASSETS}/App Icon & Top Shelf Image.brandassets/{name}.imageset"
    os.makedirs(base, exist_ok=True)
    images = []
    for scale, k in (("1x", 1), ("2x", 2)):
        w, h = w1 * k, h1 * k
        img = back_layer(w, h).convert("RGB")
        d = ImageDraw.Draw(img)
        flower(d, w * 0.5 - h * 0.62, h / 2, h * 0.30)
        font = ImageFont.truetype(font_path, int(h * 0.17))
        d.text((w * 0.5 - h * 0.22, h / 2), "Marigold", font=font, fill=(255, 255, 255), anchor="lm")
        fn = f"{name.lower().replace(' ', '-')}{'' if k == 1 else '@2x'}.png"
        img.save(f"{base}/{fn}")
        images.append({"filename": fn, "idiom": "tv", "scale": scale})
    write_json(f"{base}/Contents.json", {"images": images, "info": INFO})


def tvos(font_path):
    write_json(f"{XCASSETS}/Contents.json", {"info": INFO})
    imagestack("App Icon", [("1x", 400, 240), ("2x", 800, 480)])
    imagestack("App Icon - App Store", [("1x", 1280, 768)])
    top_shelf("Top Shelf Image", 1920, 720, font_path)
    top_shelf("Top Shelf Image Wide", 2320, 720, font_path)
    write_json(f"{XCASSETS}/App Icon & Top Shelf Image.brandassets/Contents.json", {
        "assets": [
            {"filename": "App Icon - App Store.imagestack", "idiom": "tv", "role": "primary-app-icon", "size": "1280x768"},
            {"filename": "App Icon.imagestack", "idiom": "tv", "role": "primary-app-icon", "size": "400x240"},
            {"filename": "Top Shelf Image Wide.imageset", "idiom": "tv", "role": "top-shelf-image-wide", "size": "2320x720"},
            {"filename": "Top Shelf Image.imageset", "idiom": "tv", "role": "top-shelf-image", "size": "1920x720"},
        ],
        "info": INFO,
    })


if __name__ == "__main__":
    if "--tvos" in sys.argv:
        tvos("app/fonts/InterDisplay-Medium.ttf")
    else:
        icon(80, "app/icon.png")
        icon(130, "app/icon-large.png")
        splash("app/splash.png", "app/fonts/InterDisplay-Medium.ttf")
