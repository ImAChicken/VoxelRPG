#!/usr/bin/env python3
"""Paints the furnishing tiles in every resource pack (blocks.toml
wool_white, wool_red, wool_blue, wool_green, bookshelf, iron_block,
gold_block, rug_red; atlas row 4, columns 6-13). Cloth takes a little
of each pack's plaster tone so it sits in the pack's light. Every tile
wraps round (tiles are world-aligned: 4 voxels a tile).

    python3 tools/gen_furnishing_textures.py      (from the repo root)
"""
import random
from PIL import Image

T = 16
PACKS = ["default", "darktest", "jrpg"]
TILES = {"wool_white": (6, 4), "wool_red": (7, 4), "wool_blue": (8, 4), "wool_green": (9, 4),
         "bookshelf": (10, 4), "iron_block": (11, 4), "gold_block": (12, 4), "rug_red": (13, 4)}
PLASTER, PLANKS = (3, 14), (2, 2)


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


def weave(base, rng):
    """woollen cloth: a tight over-under weave, slubs here and there"""
    px = [[base] * T for _ in range(T)]
    for y in range(T):
        for x in range(T):
            over = ((x // 2) + (y // 2)) % 2 == 0
            k = 1.05 if over else 0.93
            if (x % 2 == 0) == over:
                k *= 0.97
            px[y][x] = mul(base, k * (0.97 + 0.06 * rng.random()))
    for _ in range(5):                                   # slubs
        x, y = rng.randrange(T), rng.randrange(T)
        for i in range(3):
            px[y][(x + i) % T] = mul(px[y][(x + i) % T], 1.08)
    return px


def bookshelf(wood, rng):
    """two shelves of book spines in a wooden case; the case's sides at
    the tile's edges so a row of shelves reads as one case"""
    dark = mul(wood, 0.55)
    px = [[wood] * T for _ in range(T)]
    colours = [(128, 40, 36), (44, 70, 120), (52, 96, 56), (150, 120, 60), (90, 50, 90), (170, 150, 120), (60, 44, 36)]
    for (y0, y1) in ((1, 7), (9, 15)):
        x = 1
        while x < T - 1:
            w = rng.choice((1, 2, 2, 2))
            if x + w > T - 1:
                w = T - 1 - x
            c = rng.choice(colours)
            top = y0 + rng.choice((0, 0, 1, 2))           # some books shorter
            for xx in range(x, x + w):
                for y in range(y0, y1):
                    if y < top:
                        px[y][xx] = dark                  # the shadow above a short book
                    else:
                        k = 1.1 if xx == x else 0.95
                        if y == top + 1 or y == y1 - 2:
                            k *= 0.8                      # bands on the spine
                        px[y][xx] = mul(c, k)
            x += w
        for x in range(T):
            px[y1][x] = mul(wood, 0.8)                    # the shelf board
    for y in range(T):
        px[y][0] = mul(wood, 0.85)
        px[y][T - 1] = mul(wood, 0.7)
    for x in range(T):
        px[0][x] = mul(wood, 0.9)
    return px


def metal(base, rng, shine):
    """worked metal: hammer marks, a brighter edge, a rivet each corner"""
    px = [[mul(base, 0.92 + 0.12 * rng.random()) for _ in range(T)] for _ in range(T)]
    for y in range(T):
        for x in range(T):
            if x in (0, T - 1) or y in (0, T - 1):
                px[y][x] = mul(base, 0.75)
            elif x in (1,) or y in (1,):
                px[y][x] = mul(base, 1.0 + shine)
    for _ in range(7):                                    # hammer marks
        cx, cy = rng.randrange(3, T - 3), rng.randrange(3, T - 3)
        px[cy][cx] = mul(base, 1.0 + shine * 1.5)
        px[cy + 1][cx + 1] = mul(base, 0.8)
    for (rx, ry) in ((3, 3), (T - 4, 3), (3, T - 4), (T - 4, T - 4)):
        px[ry][rx] = mul(base, 1.0 + shine * 2)
        px[ry + 1][rx] = mul(base, 0.7)
    return px


def rug(field, border, accent, rng):
    """a woven rug: a medallion of diamonds on a red field, a band of
    the border colour along two edges (rugs are laid in strips)"""
    px = [[field] * T for _ in range(T)]
    for y in range(T):
        for x in range(T):
            d = abs(x - 7.5) + abs(y - 7.5)
            if 5.0 <= d < 6.0:
                c = border
            elif d < 2.0:
                c = accent
            elif 3.0 <= d < 3.8:
                c = accent
            else:
                c = field
            k = 0.95 + 0.08 * (((x // 2) + (y // 2)) % 2) + 0.03 * rng.random()
            px[y][x] = mul(c, k)
    return px


for i, pack in enumerate(PACKS):
    path = f"data/resourcepacks/{pack}/atlas.png"
    im = Image.open(path).convert("RGBA")
    plaster, planks = avg(im, PLASTER), avg(im, PLANKS)
    rng = random.Random(733 + i)
    tint = lambda c: mix(c, plaster, 0.12)
    put = {
        "wool_white": weave(tint((228, 222, 206)), rng),
        "wool_red": weave(tint((150, 42, 40)), rng),
        "wool_blue": weave(tint((56, 76, 142)), rng),
        "wool_green": weave(tint((62, 110, 66)), rng),
        "bookshelf": bookshelf(mul(planks, 0.7), rng),
        "iron_block": metal((70, 70, 76), rng, 0.25),
        "gold_block": metal((206, 164, 66), rng, 0.2),
        "rug_red": rug(tint((136, 36, 34)), (212, 168, 80), (52, 70, 128), rng),
    }
    for k, tile in TILES.items():
        x0, y0 = tile[0] * T, tile[1] * T
        for y in range(T):
            for x in range(T):
                im.putpixel((x0 + x, y0 + y), put[k][y][x] + (255,))
    im.save(path)
    print(f"{pack}: furnishing tiles painted")
