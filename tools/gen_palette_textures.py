#!/usr/bin/env python3
"""Paints the building-palette tiles in every resource pack (blocks.toml
whitewash, ochre_plaster, dark_planks, fieldstone, herringbone_bricks;
atlas row 3, columns 9-13). Colours follow each pack's own plaster,
planks, stone and brick tiles, so a pack keeps its look. Every tile
wraps round (tiles are world-aligned: 4 voxels a tile).

    python3 tools/gen_palette_textures.py      (from the repo root)
"""
import random
from PIL import Image

T = 16
PACKS = ["default", "darktest", "jrpg"]
TILES = {"whitewash": (9, 3), "ochre": (10, 3), "dark_planks": (11, 3), "fieldstone": (12, 3), "herringbone": (13, 3)}
PLASTER, PLANKS, COBBLE, BRICKS = (3, 14), (2, 2), (4, 0), (0, 3)


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


def wash(base, rng, streaks=0.03):
    """limewash: soft, a few brush strokes, flecks"""
    px = [[mul(base, 0.98 + 0.03 * rng.random()) for _ in range(T)] for _ in range(T)]
    for _ in range(6):                                  # brush strokes run across
        y, x0, k = rng.randrange(T), rng.randrange(T), 1.0 + (streaks if rng.random() < 0.5 else -streaks)
        for i in range(rng.randrange(4, 9)):
            px[y][(x0 + i) % T] = mul(px[y][(x0 + i) % T], k)
    for _ in range(4):                                  # flecks where it's thin
        x, y = rng.randrange(T), rng.randrange(T)
        px[y][x] = mul(base, 0.88)
    return px


def dark_planks(base, rng):
    """three boards a tile, joints staggered, grain"""
    px = [[base for _ in range(T)] for _ in range(T)]
    rows = [(0, 5), (5, 10), (10, 16)]                  # board edges (16 wraps to 0)
    joints = [3, 11, None]
    for b, (y0, y1) in enumerate(rows):
        tone = 0.92 + rng.random() * 0.16
        for y in range(y0, y1):
            for x in range(T):
                g = 1.0 + 0.05 * ((hash((b, y, x // 4)) % 3) - 1)
                px[y][x] = mul(base, tone * g)
        for _ in range(2):
            gy, gx = rng.randrange(y0, y1), rng.randrange(T)
            for k in range(rng.randrange(4, 8)):
                px[gy][(gx + k) % T] = mul(px[gy][(gx + k) % T], 0.85)
        for x in range(T):
            px[y1 - 1 if y1 < T else T - 1][x] = mul(base, 0.55)     # the gap under the board
        if joints[b] is not None:
            for y in range(y0, y1):
                px[y][joints[b]] = mul(base, 0.55)
    return px


def fieldstone(stone, mortar, rng):
    """rounded stones of a few sizes in light mortar; wraps"""
    px = [[mortar for _ in range(T)] for _ in range(T)]
    seeds = [(2, 2, 3.2), (9, 1, 3.0), (14, 6, 2.6), (5, 8, 3.4), (12, 11, 3.0), (1, 13, 2.4), (8, 14, 2.2)]
    for sx, sy, r in seeds:
        tone = mix(stone, (stone[0] * 1.1, stone[1] * 1.0, stone[2] * 0.92), rng.random())
        tone = mul(tone, 0.85 + 0.3 * rng.random())
        for y in range(T):
            for x in range(T):
                dx = min(abs(x - sx), T - abs(x - sx))
                dy = min(abs(y - sy), T - abs(y - sy))
                d = (dx * dx + dy * dy * 1.3) ** 0.5
                if d < r:
                    lit = 1.08 if (y - sy) % T > T // 2 or y < sy else 0.92   # top lit, bottom shaded
                    edge = 0.85 if d > r - 0.9 else 1.0
                    px[y][x] = mul(tone, lit * edge * (0.95 + 0.1 * rng.random()))
    return px


def herringbone(brick, mortar, rng):
    """bricks 8 x 4 (2 x 1 voxels) in a zigzag: horizontal and upright ones
    stepping down a diagonal, the bands side by side; wraps in 16"""
    own = [[None] * T for _ in range(T)]
    for m in range(-4, 4):
        for k in range(-8, 8):
            for kind, (x0, y0, w, h) in (("H", (4 * k + 8 * m, 4 * k - 8 * m, 8, 4)),
                                         ("V", (4 * k + 8 * m, 4 * k + 4 - 8 * m, 4, 8))):
                for y in range(y0, y0 + h):
                    for x in range(x0, x0 + w):
                        own[y % T][x % T] = (kind, k, m, x0 % T, y0 % T)
    px = [[mortar for _ in range(T)] for _ in range(T)]
    for y in range(T):
        for x in range(T):
            b = own[y][x]
            right, below = own[y][(x + 1) % T], own[(y + 1) % T][x]
            if right != b or below != b:
                px[y][x] = mortar                       # the joint on the brick's right and lower side
                continue
            k = 0.88 + 0.24 * ((hash(b) % 7) / 6.0)
            top = own[(y - 1) % T][x] != b
            px[y][x] = mul(brick, k * (1.08 if top else 1.0))
    return px


STAINED = {"stained_glass_red": ((14, 3), (176, 52, 48)), "stained_glass_amber": ((15, 3), (214, 150, 52)),
           "stained_glass_green": ((4, 4), (70, 150, 82)), "stained_glass_blue": ((5, 4), (64, 96, 186))}


def stained(pane, rng):
    """a leaded window: small diamonds of coloured glass, each a
    little lighter or darker, in dark lead; wraps"""
    lead = (40, 40, 44)
    px = [[pane for _ in range(T)] for _ in range(T)]
    for y in range(T):
        for x in range(T):
            if (x + y) % 8 == 0 or (x - y) % 8 == 0:
                px[y][x] = lead
            else:
                cell = ((x + y) // 8, (x - y) // 8)
                k = 0.85 + 0.3 * ((hash(cell) % 7) / 6.0)
                if (x + y) % 8 == 2 and (x - y) % 8 in (2, 3):
                    k *= 1.25                               # a glint
                px[y][x] = mul(pane, k)
    return px


for i, pack in enumerate(PACKS):
    path = f"data/resourcepacks/{pack}/atlas.png"
    im = Image.open(path).convert("RGBA")
    plaster, planks = avg(im, PLASTER), avg(im, PLANKS)
    stone, brick = avg(im, COBBLE), avg(im, BRICKS)
    rng = random.Random(311 + i)
    white = mix(plaster, (240, 238, 230), 0.7)
    ochre = mix(plaster, (204, 156, 84), 0.75)
    dark = (planks[0] * 0.55, planks[1] * 0.5, planks[2] * 0.48)
    mortar = mix(stone, (200, 194, 180), 0.6)
    put = {
        "whitewash": wash(white, rng),
        "ochre": wash(ochre, rng, 0.05),
        "dark_planks": dark_planks(dark, rng),
        "fieldstone": fieldstone(stone, mortar, rng),
        "herringbone": herringbone(brick, mix(brick, (190, 180, 165), 0.6), rng),
    }
    for name, (tile, col) in STAINED.items():
        put[name] = stained(col, rng)
        TILES[name] = tile
    for k, tile in TILES.items():
        x0, y0 = tile[0] * T, tile[1] * T
        for y in range(T):
            for x in range(T):
                im.putpixel((x0 + x, y0 + y), put[k][y][x] + (255,))
    im.save(path)
    print(f"{pack}: palette tiles painted")
