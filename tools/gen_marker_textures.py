#!/usr/bin/env python3
"""Paints the dungeon marker tiles in every resource pack (blocks.toml
marker_spawner … marker_secret; atlas row 15, columns 1-7) and adds
their face_map entries. A marker is a placeholder that turns into the
real thing in a dungeon (dungeon/Tile.h), so it looks like one: bold
diagonal stripes in its colour with a dark symbol in the middle, the
same in every pack.

    python3 tools/gen_marker_textures.py      (from the repo root)
"""
from PIL import Image

T = 16
PACKS = ["default", "darktest", "jrpg"]
# kind: (column, colour, symbol rows (8x8, '#' dark))
MARKERS = {
    "spawner": (1, (215, 45, 40), ["..####..", ".##..##.", ".#.##.#.", ".#.##.#.", ".##..##.", "..####..", "..#..#..", ".##..##."]),
    "chest":   (2, (220, 165, 30), ["........", "########", "#......#", "###..###", "#..##..#", "#......#", "########", "........"]),
    "portal":  (3, (140, 70, 240), ["..####..", ".#....#.", "#..##..#", "#.#..#.#", "#.#..#.#", "#..##..#", ".#....#.", "..####.."]),
    "exit":    (4, (55, 215, 240), ["...#....", "..###...", ".#.#.#..", "...#....", "...#....", "...#....", ".#####..", "........"]),
    "boss":    (5, (240, 30, 140), ["#..##..#", "##.##.##", "########", "#.####.#", "########", ".######.", ".#.##.#.", "........"]),
    "coffin":  (6, (120, 80, 50), ["..####..", ".#....#.", "#......#", "#..##..#", "#......#", ".#....#.", ".#....#.", "..####.."]),
    "secret":  (7, (140, 140, 128), ["..####..", ".#....#.", ".....#..", "....#...", "...#....", "........", "...#....", "........"]),
}


def tile(colour, symbol):
    light = tuple(min(255, int(c * 1.15)) for c in colour)
    dark = tuple(int(c * 0.72) for c in colour)
    ink = tuple(int(c * 0.22) for c in colour)
    px = [[colour] * T for _ in range(T)]
    for y in range(T):
        for x in range(T):
            px[y][x] = light if ((x + y) // 3) % 2 == 0 else dark
    for y in range(8):
        for x in range(8):
            if symbol[y][x] == "#":
                for dy in range(2):
                    for dx in range(2):
                        px[y * 2 + dy][x * 2 + dx] = ink
    for i in range(T):                               # a frame, so neighbours read as separate blocks
        px[0][i] = px[T - 1][i] = px[i][0] = px[i][T - 1] = ink
    return px


for pack in PACKS:
    path = f"data/resourcepacks/{pack}/atlas.png"
    im = Image.open(path).convert("RGBA")
    for kind, (col, colour, sym) in MARKERS.items():
        px = tile(colour, sym)
        for y in range(T):
            for x in range(T):
                im.putpixel((col * T + x, 15 * T + y), px[y][x] + (255,))
    im.save(path)
    cfg = f"data/resourcepacks/{pack}/atlas_config.toml"
    text = open(cfg).read()
    for kind, (col, _, _) in MARKERS.items():
        name = "marker_" + kind
        if f'name    = "{name}"' in text:
            continue
        text += f'\n[[face_map]]\nname    = "{name}"\ntop     = [{col}, 15]\nside    = [{col}, 15]\nbottom  = [{col}, 15]\n'
    open(cfg, "w").write(text)
    print(f"{pack}: marker tiles painted")
