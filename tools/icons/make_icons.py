#!/usr/bin/env python3
"""Draw the page menu's icons, day and night, as 96 x 96 PNGs for SquareLine Studio.

One navy squircle tile per page with one bold white glyph; the night set is the same geometry in
pure red on black (G = B = 0, as NightPalette.h requires). The PNGs are opaque: their antialiased
edges are blended into the page background (day 0x15171A, night black), so SquareLine keeps
exporting them as RGB565 and no black square shows around a tile.

Each glyph is drawn 4x oversampled on a 96-unit grid and box-filtered down.

Run: python3 tools/icons/make_icons.py [--out DIR] [--preview FILE]
Default out is SLS_project_WS/assets; export from SquareLine afterwards (commit first).
"""

import argparse
import math
import pathlib

from PIL import Image, ImageChops, ImageDraw, ImageFilter

SIZE = 96
OS = 4                      # oversampling
HI = SIZE * OS

# Page background behind the menu. Day: LVGL's dark theme screen color; night: true black.
DAY_BG = (0x15, 0x17, 0x1A)
NIGHT_BG = (0, 0, 0)

# Day tile: deep sea navy, lighter at the top
DAY_TOP = (0x1E, 0x4E, 0x8C)
DAY_BOTTOM = (0x0B, 0x24, 0x47)
DAY_GLYPH = (255, 255, 255)
DAY_SECONDARY = 0.55        # opacity of a glyph's secondary parts
DAY_SHEEN = 0.30            # opacity of the light rim along the tile's top

# Night: NightPalette.h's named shades
RED_MID = (0xB0, 0, 0)
RED_DARK = (0x70, 0, 0)

TILE_HALF = 46              # squircle half size in units (2-unit margin)
TILE_EXP = 5.0              # superellipse exponent
RIM = 2                     # night tile outline, units

C = SIZE / 2                # center


# --- Mask helpers, coordinates in 96-unit space ---

def mask():
    return Image.new("L", (HI, HI), 0)


def s(v):
    return v * OS


def pts(points):
    return [(s(x), s(y)) for x, y in points]


def disc(m, cx, cy, r, fill=255):
    ImageDraw.Draw(m).ellipse([s(cx - r), s(cy - r), s(cx + r), s(cy + r)], fill=fill)


def ring(m, cx, cy, r, w):
    ImageDraw.Draw(m).ellipse([s(cx - r), s(cy - r), s(cx + r), s(cy + r)], outline=255, width=round(s(w)))


def poly(m, points, fill=255):
    ImageDraw.Draw(m).polygon(pts(points), fill=fill)


def rect(m, x0, y0, x1, y1, r=0, fill=255):
    ImageDraw.Draw(m).rounded_rectangle([s(x0), s(y0), s(x1), s(y1)], radius=s(r), fill=fill)


def line(m, x0, y0, x1, y1, w):
    """Line with round caps"""
    ImageDraw.Draw(m).line([s(x0), s(y0), s(x1), s(y1)], fill=255, width=round(s(w)))
    disc(m, x0, y0, w / 2)
    disc(m, x1, y1, w / 2)


def polar(cx, cy, r, deg):
    """Point at bearing deg (0 = up, clockwise)"""
    a = math.radians(deg)
    return cx + r * math.sin(a), cy - r * math.cos(a)


def union(a, b):
    return ImageChops.lighter(a, b)


def minus(a, b):
    return ImageChops.subtract(a, b)


def grow(m, units):
    """Dilate a mask, to cut a gap around a shape that overlaps another"""
    k = round(s(units)) * 2 + 1
    return m.filter(ImageFilter.MaxFilter(k))


def sparkle(m, cx, cy, r, waist=0.3):
    p = []
    for i in range(8):
        rr = r if i % 2 == 0 else r * waist
        p.append(polar(cx, cy, rr, i * 45))
    poly(m, p)


# --- Glyphs: each returns (primary, secondary) masks ---

def g_navigation():
    pri, sec = mask(), mask()
    ring(pri, C, C, 27, 4)
    poly(pri, [(C, 24), (C + 8, C), (C - 8, C)])
    poly(sec, [(C, 72), (C + 8, C), (C - 8, C)])
    return pri, sec


def g_batteries():
    """A boat's lead-acid battery: two posts on top, + and - on its face, not the watch's own"""
    pri, sec = mask(), mask()
    rect(pri, 20, 37, 76, 69, r=5)            # case
    rect(pri, 28, 30, 38, 38, r=1.5)          # posts
    rect(pri, 58, 30, 68, 38, r=1.5)
    cut = mask()
    rect(cut, 20, 43, 76, 46)                 # lid seam
    line(cut, 27, 57, 39, 57, 4)              # +
    line(cut, 33, 51, 33, 63, 4)
    line(cut, 57, 57, 69, 57, 4)              # -
    return minus(pri, cut), sec


def g_weather():
    pri, sec = mask(), mask()
    sun = mask()
    sx, sy = 59, 37
    disc(sun, sx, sy, 8)
    for i in range(8):
        x0, y0 = polar(sx, sy, 12.5, i * 45)
        x1, y1 = polar(sx, sy, 17, i * 45)
        line(sun, x0, y0, x1, y1, 4)
    cloud = mask()
    disc(cloud, 35, 58, 10)
    disc(cloud, 48, 50, 14)
    disc(cloud, 61, 58, 10)
    rect(cloud, 35, 54, 61, 68)
    pri = union(minus(sun, grow(cloud, 3)), cloud)
    return pri, sec


def g_engine():
    pri, sec = mask(), mask()
    rect(pri, 31, 40, 62, 66, r=3)            # block
    rect(pri, 42, 33, 51, 41)                 # oil filler neck
    rect(pri, 36, 28, 57, 33, r=1.5)          # filler cap
    rect(pri, 24, 49, 32, 57)                 # intake pipe
    rect(pri, 20, 44, 26, 62, r=1.5)          # intake flange
    poly(pri, [(61, 46), (69, 40), (75, 40), (75, 66), (69, 66), (61, 60)])   # manifold
    hole = mask()
    rect(hole, 37, 46, 56, 50, r=1.5)         # cooling slot, reads as an engine at a glance
    rect(hole, 37, 55, 56, 59, r=1.5)
    pri = minus(pri, hole)
    return pri, sec


def g_attitude():
    """A seesaw: a plank tilted on its fulcrum"""
    pri, sec = mask(), mask()
    px, py, tilt, half = C, 44, 16, 28
    a = math.radians(tilt)
    dx, dy = math.cos(a), math.sin(a)
    plank = mask()
    line(plank, px - half * dx, py - half * dy, px + half * dx, py + half * dy, 8)
    poly(pri, [(px, py + 5), (px + 13, 69), (px - 13, 69)])        # fulcrum
    pri = union(minus(pri, grow(plank, 2)), plank)
    return pri, sec


def g_clock():
    pri, sec = mask(), mask()
    ring(pri, C, C, 27, 5)
    for deg in (0, 90, 180, 270):
        x0, y0 = polar(C, C, 17, deg)
        x1, y1 = polar(C, C, 20, deg)
        line(pri, x0, y0, x1, y1, 4)
    hx, hy = polar(C, C, 11, 300)
    line(pri, C, C, hx, hy, 6)
    mx, my = polar(C, C, 17, 60)
    line(pri, C, C, mx, my, 4.5)
    disc(pri, C, C, 4)
    return pri, sec


def g_setup():
    pri, sec = mask(), mask()
    disc(pri, C, C, 20)
    for i in range(8):
        deg = i * 45
        half = 5.5
        p = []
        for r, side in ((14, -1), (28, -1), (28, 1), (14, 1)):
            a = math.radians(deg)
            # tooth centered on bearing deg, slightly narrower at the tip
            w = half * (0.8 if r == 28 else 1.0)
            x, y = polar(C, C, r, deg)
            p.append((x + side * w * math.cos(a), y + side * w * math.sin(a)))
        poly(pri, p)
    hole = mask()
    disc(hole, C, C, 8)
    return minus(pri, hole), sec


def g_celestial():
    pri, sec = mask(), mask()
    moon = mask()
    disc(moon, 43, 52, 21)
    bite = mask()
    disc(bite, 54, 44, 18)
    pri = minus(moon, bite)
    sparkle(pri, 66, 30, 9)
    sparkle(pri, 71, 54, 6)
    return pri, sec


def g_chart():
    pri, sec = mask(), mask()
    top = [(22, 31), (38.5, 26), (57.5, 31), (74, 26)]
    bot = [(22, 70), (38.5, 65), (57.5, 70), (74, 65)]
    gap = 1.4
    for i in range(3):
        x0, x1 = top[i][0] + gap, top[i + 1][0] - gap

        def at(edge, x):
            (ax, ay), (bx, by) = edge[i], edge[i + 1]
            return ay + (by - ay) * (x - ax) / (bx - ax)

        p = [(x0, at(top, x0)), (x1, at(top, x1)), (x1, at(bot, x1)), (x0, at(bot, x0))]
        poly(sec if i == 1 else pri, p)
    # Position pin on the middle panel, cut free of it
    pin = mask()
    disc(pin, C, 43, 8)
    poly(pin, [(C - 7, 46), (C + 7, 46), (C, 59)])
    hole = mask()
    disc(hole, C, 43, 3.2)
    pin = minus(pin, hole)
    sec = minus(sec, grow(pin, 2))
    pri = union(minus(pri, grow(pin, 2)), pin)
    return pri, sec


ICONS = {
    "navigation": g_navigation,
    "batteries": g_batteries,
    "weather": g_weather,
    "engine": g_engine,
    "attitude": g_attitude,
    "clock": g_clock,
    "setup": g_setup,
    "celestial": g_celestial,
    "chart": g_chart,
}


# --- Tile and composition ---

def tile_mask():
    m = Image.new("L", (HI, HI), 0)
    p = []
    for i in range(720):
        t = 2 * math.pi * i / 720
        c, sn = math.cos(t), math.sin(t)
        x = math.copysign(abs(c) ** (2 / TILE_EXP), c) * TILE_HALF
        y = math.copysign(abs(sn) ** (2 / TILE_EXP), sn) * TILE_HALF
        p.append((s(C + x), s(C + y)))
    ImageDraw.Draw(m).polygon(p, fill=255)
    return m


def solid(color):
    return Image.new("RGB", (HI, HI), color)


def render(name, night):
    pri, sec = ICONS[name]()
    tile = tile_mask()
    img = solid(NIGHT_BG if night else DAY_BG)
    inner = tile.filter(ImageFilter.MinFilter(s(RIM) * 2 + 1))
    if night:
        img.paste(RED_DARK, mask=minus(tile, inner))
        img.paste(RED_DARK, mask=ImageChops.multiply(sec, inner))
        img.paste(RED_MID, mask=ImageChops.multiply(pri, inner))
    else:
        grad = Image.new("RGB", (1, HI))
        for y in range(HI):
            f = y / (HI - 1)
            grad.putpixel((0, y), tuple(round(a + (b - a) * f) for a, b in zip(DAY_TOP, DAY_BOTTOM)))
        img.paste(grad.resize((HI, HI)), mask=tile)
        # Sheen: a light rim along the top, fading out by mid height
        rim = minus(tile, tile.filter(ImageFilter.MinFilter(s(1) * 2 + 1)))
        fade = Image.linear_gradient("L").resize((HI, HI)).point(
            lambda v: max(0, round((1 - v / 127.5) * 255 * DAY_SHEEN)))
        img.paste(DAY_GLYPH, mask=ImageChops.multiply(rim, fade))
        img.paste(DAY_GLYPH, mask=ImageChops.multiply(sec, tile).point(lambda v: round(v * DAY_SECONDARY)))
        img.paste(DAY_GLYPH, mask=ImageChops.multiply(pri, tile))
    return img.resize((SIZE, SIZE), Image.Resampling.BOX)


def check(img, night, name):
    bg = NIGHT_BG if night else DAY_BG
    for corner in ((0, 0), (SIZE - 1, 0), (0, SIZE - 1), (SIZE - 1, SIZE - 1)):
        if img.getpixel(corner) != bg:
            raise SystemExit(f"{name}: corner {corner} is not the background")
    if night:
        _, g, b = img.split()
        if g.getbbox() or b.getbbox():
            raise SystemExit(f"night {name}: green or blue in a night icon")


def preview(icons, path):
    """Both sets in the menu's 3 x 3 layout, full size and at 60 %"""
    order = ["navigation", "batteries", "weather", "engine", "attitude",
             "setup", "clock", "celestial", "chart"]
    gap = 24
    pane = 3 * SIZE + 4 * gap
    small = round(SIZE * 0.6)
    spane = 3 * small + 4 * round(gap * 0.6)
    sheet = Image.new("RGB", (2 * pane + 2 * spane + 3 * gap, pane), (60, 60, 60))
    x = 0
    for night in (False, True):
        for size, width in ((SIZE, pane), (small, spane)):
            bg = Image.new("RGB", (width, pane), NIGHT_BG if night else DAY_BG)
            g = (width - 3 * size) // 4
            for i, n in enumerate(order):
                im = icons[(n, night)]
                if size != SIZE:
                    im = im.resize((size, size), Image.Resampling.LANCZOS)
                bg.paste(im, (g + (i % 3) * (size + g), g + (i // 3) * (size + g)))
            sheet.paste(bg, (x, 0))
            x += width + gap
    sheet.save(path)


def main():
    root = pathlib.Path(__file__).resolve().parents[2]
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--out", type=pathlib.Path, default=root / "SLS_project_WS/assets")
    parser.add_argument("--preview", type=pathlib.Path)
    args = parser.parse_args()

    icons = {}
    for name in ICONS:
        for night in (False, True):
            img = render(name, night)
            check(img, night, name)
            icons[(name, night)] = img
    args.out.mkdir(parents=True, exist_ok=True)
    for (name, night), img in icons.items():
        img.save(args.out / f"{'night' if night else ''}{name}.png")
    if args.preview:
        preview(icons, args.preview)
    print(f"{len(icons)} icons written to {args.out}")


if __name__ == "__main__":
    main()
