#!/usr/bin/env python3
"""Paints the Mushroom Islands tiles in every resource pack (blocks.toml
glowcap_blue, glowcap_pink, glowcap_green, mycelium, grapple_point;
atlas row 2, columns 5-10). The mycelium follows each pack's own dirt
and grass, so a pack keeps its look. Every tile wraps round.

    python3 tools/gen_sky_textures.py      (from the repo root)
(Claude, 2026-11-25)
"""
import random
from PIL import Image

T = 16
PACKS = ["default", "darktest", "jrpg"]
DIRT, GRASS_TOP = (0, 1), (1, 1)
TILES = {"glowcap_blue": (5, 2), "glowcap_pink": (6, 2), "glowcap_green": (7, 2),
         "mycelium_top": (8, 2), "mycelium_side": (9, 2), "grapple_point": (10, 2)}


def clamp(v):
    return max(0, min(255, int(round(v))))


def mul(c, k):
    return tuple(clamp(x * k) for x in c)


def mix(a, b, t):
    return tuple(clamp(a[i] * (1 - t) + b[i] * t) for i in range(3))


def avg(im, tile):
    x0, y0 = tile[0] * T, tile[1] * T
    px = [im.getpixel((x0 + x, y0 + y)) for y in range(T) for x in range(T)]
    return tuple(sum(p[k] for p in px) / len(px) for k in range(3))


def glowcap(base, spot, rng):
    """a glowing cap: soft mottling, bright spots like the reference's dotted caps"""
    px = [[mul(base, 0.86 + 0.2 * rng.random()) for _ in range(T)] for _ in range(T)]
    for _ in range(7):
        cx, cy, r = rng.randrange(T), rng.randrange(T), rng.choice([1, 1, 2])
        for y in range(T):
            for x in range(T):
                dx = min(abs(x - cx), T - abs(x - cx))
                dy = min(abs(y - cy), T - abs(y - cy))
                if dx * dx + dy * dy <= r * r:
                    px[y][x] = mix(spot, (255, 255, 255), 0.25 * rng.random())
    return px


def mycelium_top(dirt, rng):
    """a fungal mat: violet-grey threads over dark soil"""
    base = mix(dirt, (110, 92, 128), 0.7)
    px = [[mul(base, 0.85 + 0.25 * rng.random()) for _ in range(T)] for _ in range(T)]
    for _ in range(10):                     # pale threads
        x, y = rng.randrange(T), rng.randrange(T)
        for i in range(rng.randrange(3, 7)):
            px[y % T][(x + i) % T] = mix(base, (200, 190, 215), 0.45)
            if rng.random() < 0.4:
                y += 1
    return px


def mycelium_side(dirt, top, rng):
    px = [[mul(dirt, 0.9 + 0.2 * rng.random()) for _ in range(T)] for _ in range(T)]
    for x in range(T):
        depth = 3 + (1 if rng.random() < 0.4 else 0)
        for y in range(depth):
            px[y][x] = top[y][x]
    return px


def grapple_point():
    """a knot of rope round a ring: easy to pick out"""
    rope, ring, dark = (205, 170, 95), (150, 150, 160), (70, 55, 35)
    px = [[mul(rope, 0.75) for _ in range(T)] for _ in range(T)]
    for y in range(T):
        for x in range(T):
            if (x + y) % 4 == 0:
                px[y][x] = mul(rope, 1.1)          # the rope's twist
            elif (x + y) % 4 == 2:
                px[y][x] = dark
            d = ((x - 7.5) ** 2 + (y - 7.5) ** 2) ** 0.5
            if 4.5 < d < 6.5:
                px[y][x] = ring
    return px


for i, pack in enumerate(PACKS):
    path = f"data/resourcepacks/{pack}/atlas.png"
    im = Image.open(path).convert("RGBA")
    dirt = avg(im, DIRT)
    rng = random.Random(4041 + i)
    top = mycelium_top(dirt, rng)
    put = {
        "glowcap_blue": glowcap((55, 120, 235), (170, 230, 255), rng),
        "glowcap_pink": glowcap((230, 95, 190), (255, 200, 240), rng),
        "glowcap_green": glowcap((105, 225, 130), (220, 255, 170), rng),
        "mycelium_top": top,
        "mycelium_side": mycelium_side(dirt, top, rng),
        "grapple_point": grapple_point(),
    }
    for k, tile in TILES.items():
        x0, y0 = tile[0] * T, tile[1] * T
        for y in range(T):
            for x in range(T):
                im.putpixel((x0 + x, y0 + y), put[k][y][x] + (255,))
    im.save(path)
    print(f"{pack}: sky tiles painted")
