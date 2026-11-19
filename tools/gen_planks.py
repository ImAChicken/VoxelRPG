#!/usr/bin/env python3
"""Paints the wood plank tile (atlas tile [2, 2], blocks.toml "planks")
in every resource pack: four boards a tile, a dark gap between them,
end joints staggered board to board, grain along each board, a knot,
nail heads by the joints. Tiles with itself in both directions. The
colour comes from each pack's current tile (its average), so a pack
keeps its look.

    python3 tools/gen_planks.py            (from the repo root)
"""
import random
from PIL import Image

T = 16                      # tile size (atlas_config.toml tile_size)
TILE = (2, 2)               # the planks tile
PACKS = ["default", "darktest", "jrpg"]


def clamp(v):
    return max(0, min(255, int(round(v))))


def shade(c, k):
    return tuple(clamp(x * k) for x in c)


def paint(base, seed):
    rng = random.Random(seed)
    px = [[base for _ in range(T)] for _ in range(T)]
    # four boards, rows 0-2, 4-6, 8-10, 12-14; row 3, 7, 11, 15 is the gap
    joints = [5, None, 12, None]                 # where boards' ends meet (two a tile: long boards)
    for b in range(4):
        tone = 0.9 + rng.random() * 0.2          # each board a little different
        y0 = b * 4
        for y in range(y0, y0 + 3):
            for x in range(T):
                # grain: soft streaks along the board, wrapping round the tile
                g = 1.0 + 0.06 * ((hash((b, y, x // 3, seed)) % 5) - 2) / 2.0
                if y == y0:
                    g *= 1.07                    # the top edge catches the light
                if y == y0 + 2:
                    g *= 0.92                    # the lower edge in a little shade
                px[y][x] = shade(base, tone * g)
        # a few darker grain lines running part of the board
        for _ in range(2):
            gy = y0 + rng.randrange(3)
            gx = rng.randrange(T)
            for k in range(rng.randrange(4, 9)):
                x = (gx + k) % T
                px[gy][x] = shade(px[gy][x], 0.86)
        # the joint: a dark seam across the board, a lighter edge beside it
        j = joints[b]
        if j is None:
            for x in range(T):
                px[y0 + 3][x] = shade(base, 0.5)
            continue
        for y in range(y0, y0 + 3):
            px[y][j] = shade(base, 0.55)
            px[y][(j + 1) % T] = shade(px[y][(j + 1) % T], 1.08)
        # a nail head each side of the joint
        for nx in ((j - 2) % T, (j + 3) % T):
            px[y0 + 1][nx] = shade(base, 0.5)
        # the gap below the board
        for x in range(T):
            px[y0 + 3][x] = shade(base, 0.5)
    # one knot
    ky, kx = 9, 6
    px[ky][kx] = shade(base, 0.6)
    px[ky][kx + 1] = shade(base, 0.7)
    px[ky - 1][kx] = shade(px[ky - 1][kx], 0.85)
    return px


for i, pack in enumerate(PACKS):
    path = f"data/resourcepacks/{pack}/atlas.png"
    im = Image.open(path).convert("RGBA")
    x0, y0 = TILE[0] * T, TILE[1] * T
    # the pack's wood colour: the old tile's average, a touch warmer
    cols = [im.getpixel((x0 + x, y0 + y))[:3] for y in range(T) for x in range(T)]
    avg = tuple(sum(c[k] for c in cols) / len(cols) for k in range(3))
    base = (clamp(avg[0] * 1.02), clamp(avg[1] * 0.97), clamp(avg[2] * 0.9))
    px = paint(base, 1234 + i)
    for y in range(T):
        for x in range(T):
            im.putpixel((x0 + x, y0 + y), px[y][x] + (255,))
    im.save(path)
    print(f"{pack}: planks repainted from {tuple(int(v) for v in avg)}")
