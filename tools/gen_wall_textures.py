#!/usr/bin/env python3
"""Paints the wall-part tiles in every resource pack (blocks.toml
timber_post, timber_beam, wainscot, leaded_glass; atlas row 3,
columns 4-8) and freshens the plaster tile (trowel marks). Each pack
keeps its own tone: the dark timber follows the pack's log colour.

    python3 tools/gen_wall_textures.py      (from the repo root)
"""
import random
from PIL import Image

T = 16
PACKS = ["default", "darktest", "jrpg"]
TILES = {"post_side": (4, 3), "post_end": (5, 3), "beam": (6, 3), "wainscot": (7, 3), "leaded": (8, 3)}
PLASTER = (3, 14)
LOG_SIDE = (1, 2)


def clamp(v):
    return max(0, min(255, int(round(v))))


def mul(c, k):
    return tuple(clamp(x * k) for x in c)


def avg(im, tile):
    x0, y0 = tile[0] * T, tile[1] * T
    px = [im.getpixel((x0 + x, y0 + y)) for y in range(T) for x in range(T)]
    px = [p for p in px if p[3] > 0]
    return tuple(sum(p[k] for p in px) / len(px) for k in range(3))


def grain(length, across, rng, base, streak=0.85):
    """[across][length] wood: soft streaks along the length"""
    g = [[base for _ in range(length)] for _ in range(across)]
    for a in range(across):
        line = 0.92 + rng.random() * 0.16
        for l in range(length):
            g[a][l] = mul(base, line * (1.0 + 0.05 * ((hash((a, l // 4)) % 3) - 1)))
    for _ in range(4):                                  # darker grain lines
        a, l0 = rng.randrange(across), rng.randrange(length)
        for k in range(rng.randrange(5, 12)):
            l = (l0 + k) % length
            g[a][l] = mul(g[a][l], streak)
    return g


def post_side(base, rng):
    g = grain(T, T, rng, base)                          # grain runs up: [x][y]
    px = [[g[x][y] for x in range(T)] for y in range(T)]
    return px          # (no edge shading: tiles are world-aligned, 4 voxels a tile, not fitted to a post)


def post_end(base, rng):
    px = [[base for _ in range(T)] for _ in range(T)]
    c = (T - 1) / 2.0
    for y in range(T):
        for x in range(T):
            r = ((x - c) ** 2 + (y - c) ** 2) ** 0.5
            ring = 0.88 if int(r * 1.3) % 2 else 1.0     # growth rings
            edge = 0.75 if r > 6.8 else 1.0
            px[y][x] = mul(base, ring * edge * (0.97 + 0.06 * rng.random()))
    return px


def beam(base, rng):
    g = grain(T, T, rng, base)                          # grain runs along: [y][x]
    px = [[g[y][x] for x in range(T)] for y in range(T)]
    return px


def wainscot(base, rng):
    px = [[base for _ in range(T)] for _ in range(T)]
    for x in range(T):
        b = x // 4                                      # four narrow upright boards
        tone = 0.92 + ((b * 7919) % 5) * 0.04
        for y in range(T):
            k = tone * (1.0 + 0.04 * ((hash((x, y // 5)) % 3) - 1))
            if x % 4 == 0:
                k *= 0.6                                # the groove between boards
            elif x % 4 == 1:
                k *= 1.08                               # its lit edge
            px[y][x] = mul(base, k)
    return px


def leaded(pane, lead, rng):
    px = [[pane for _ in range(T)] for _ in range(T)]
    for y in range(T):
        for x in range(T):
            # diamond lattice: lines where x+y or x-y is a multiple of 8
            on = (x + y) % 8 == 0 or (x - y) % 8 == 0
            if on:
                px[y][x] = lead
            else:
                # a little colour change across each pane, a highlight
                k = 1.0 + 0.06 * ((x + 2 * y) % 5 - 2) / 2.0
                if (x + y) % 8 == 2 and (x - y) % 8 in (2, 3):
                    k = 1.25
                px[y][x] = mul(pane, k)
    return px


def plaster(base, rng):
    px = [[base for _ in range(T)] for _ in range(T)]
    for y in range(T):
        for x in range(T):
            px[y][x] = mul(base, 0.97 + 0.05 * rng.random())
    for _ in range(5):                                  # trowel sweeps: short lighter / darker arcs
        y, x0 = rng.randrange(T), rng.randrange(T)
        k = 1.04 if rng.random() < 0.5 else 0.95
        for i in range(rng.randrange(3, 7)):
            xx, yy = (x0 + i) % T, (y + (i * i) // 9) % T
            px[yy][xx] = mul(px[yy][xx], k)
    for _ in range(2):                                  # a hairline crack
        x, y = rng.randrange(T), rng.randrange(T)
        for i in range(3):
            px[(y + i) % T][(x + (i % 2)) % T] = mul(base, 0.86)
    return px


def put(im, tile, px):
    x0, y0 = tile[0] * T, tile[1] * T
    for y in range(T):
        for x in range(T):
            im.putpixel((x0 + x, y0 + y), px[y][x] + (255,))


ref = None
for i, pack in enumerate(PACKS):
    path = f"data/resourcepacks/{pack}/atlas.png"
    im = Image.open(path).convert("RGBA")
    log = avg(im, LOG_SIDE)
    # dark stained timber: the pack's log colour, darker and a touch cooler
    timber = (log[0] * 0.78, log[1] * 0.72, log[2] * 0.70)
    board = (log[0] * 1.45, log[1] * 1.38, log[2] * 1.3)
    lead = tuple(clamp(c * 0.45) for c in (90, 92, 96))
    pane = (176, 200, 196) if pack == "default" else (150, 176, 166)
    rng = random.Random(77 + i)
    put(im, TILES["post_side"], post_side(timber, rng))
    put(im, TILES["post_end"], post_end(timber, rng))
    put(im, TILES["beam"], beam(timber, rng))
    put(im, TILES["wainscot"], wainscot(board, rng))
    put(im, TILES["leaded"], leaded(pane, lead, rng))
    put(im, PLASTER, plaster(avg(im, PLASTER), rng))
    im.save(path)
    print(f"{pack}: wall tiles painted (timber from log {tuple(int(v) for v in log)})")
