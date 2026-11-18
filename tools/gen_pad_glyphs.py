#!/usr/bin/env python3
"""Make the controller button pictures (data/ui/pad_glyphs.png). Run from
the repo root:

    python3 tools/gen_pad_glyphs.py

Switch style: each picture shows WHERE the button is, not its letter, so
it reads the same on an Xbox, PlayStation or Switch pad. The four face
buttons are a diamond of dots with the one meant filled in; the D-pad a
cross with one arm filled; the shoulders the two bumpers and triggers
with one filled; the sticks two circles; Select / Guide / Start three
small shapes in the middle.

One CELL x CELL square per picture, in a row of COLS, in pad::Button
order (input/PadLayout.h), then the two sticks to move and look.
ui/PadGlyphs.h reads it; replace the PNG with drawn art any time (same
cells, same order).
"""
from PIL import Image, ImageDraw

CELL, COLS, SS = 48, 8, 4                    # pixels per cell; supersampled 4x
ORDER = ["A", "B", "X", "Y", "LB", "RB", "Back", "Start", "Guide", "L3", "R3",
         "Up", "Right", "Down", "Left", "LT", "RT", "LStick", "RStick"]
ON = (255, 255, 255, 255)                    # the button meant
OFF = (150, 156, 168, 235)                   # the others, outlined
SHADOW = (20, 22, 28, 200)


def cell(name):
    S = CELL * SS
    im = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    w = max(2, S // 22)                      # outline width
    c = S / 2

    def shape(kind, box, meant, r=0):
        box = [round(v) for v in box]
        sh = [v + w for v in box]            # a soft drop shadow
        if kind == "ellipse":
            d.ellipse(sh, fill=SHADOW)
            d.ellipse(box, fill=ON if meant else None, outline=ON if meant else OFF, width=w)
        else:
            d.rounded_rectangle(sh, r, fill=SHADOW)
            d.rounded_rectangle(box, r, fill=ON if meant else None, outline=ON if meant else OFF, width=w)

    if name in ("A", "B", "X", "Y"):
        pos = {"Y": (0, -1), "B": (1, 0), "A": (0, 1), "X": (-1, 0)}
        rr, off = S * 0.15, S * 0.29
        for k, (dx, dy) in pos.items():
            x, y = c + dx * off, c + dy * off
            shape("ellipse", (x - rr, y - rr, x + rr, y + rr), k == name)
    elif name in ("Up", "Right", "Down", "Left"):
        arm, half = S * 0.30, S * 0.12
        arms = {"Up": (c - half, c - half - arm, c + half, c - half), "Down": (c - half, c + half, c + half, c + half + arm),
                "Left": (c - half - arm, c - half, c - half, c + half), "Right": (c + half, c - half, c + half + arm, c + half)}
        shape("rect", (c - half, c - half, c + half, c + half), False, 2)
        for k, box in arms.items():
            shape("rect", box, k == name, S * 0.04)
    elif name in ("LB", "RB", "LT", "RT"):
        # the pad's top edge: a trigger (tall) behind each bumper (wide)
        for side, sx in (("L", 0), ("R", 1)):
            x0 = S * 0.08 if sx == 0 else S * 0.56
            shape("rect", (x0 + S * 0.06, S * 0.14, x0 + S * 0.30, S * 0.48), name == side + "T", S * 0.07)
            shape("rect", (x0, S * 0.56, x0 + S * 0.36, S * 0.74), name == side + "B", S * 0.08)
    elif name in ("L3", "R3", "LStick", "RStick"):
        for side, x in (("L", c - S * 0.22), ("R", c + S * 0.22)):
            meant = name.startswith(side)
            rr = S * 0.18
            shape("ellipse", (x - rr, c - rr, x + rr, c + rr), meant and name.endswith("3"))
            if meant and name.endswith("Stick"):     # a stick moved: arrows all round
                d.ellipse((x - rr, c - rr, x + rr, c + rr), outline=ON, width=w)
                for dx, dy in ((0, -1), (1, 0), (0, 1), (-1, 0)):
                    tip = (x + dx * rr * 1.75, c + dy * rr * 1.75)
                    base = (x + dx * rr * 1.25, c + dy * rr * 1.25)
                    px, py = -dy * rr * 0.35, dx * rr * 0.35
                    d.polygon([tip, (base[0] + px, base[1] + py), (base[0] - px, base[1] - py)], fill=ON)
            if meant and name.endswith("3"):          # pressed in: a dot
                d.ellipse((x - rr * 0.35, c - rr * 0.35, x + rr * 0.35, c + rr * 0.35), fill=(60, 64, 74, 255))
    elif name in ("Back", "Start", "Guide"):
        pw, ph = S * 0.22, S * 0.14
        shape("rect", (c - S * 0.40, c - ph / 2, c - S * 0.40 + pw, c + ph / 2), name == "Back", ph / 2)
        shape("rect", (c + S * 0.40 - pw, c - ph / 2, c + S * 0.40, c + ph / 2), name == "Start", ph / 2)
        rr = S * 0.10
        shape("ellipse", (c - rr, c - rr, c + rr, c + rr), name == "Guide")
    return im.resize((CELL, CELL), Image.LANCZOS)


if __name__ == "__main__":
    rows = (len(ORDER) + COLS - 1) // COLS
    sheet = Image.new("RGBA", (COLS * CELL, rows * CELL), (0, 0, 0, 0))
    for i, n in enumerate(ORDER):
        sheet.paste(cell(n), ((i % COLS) * CELL, (i // COLS) * CELL))
    sheet.save("data/ui/pad_glyphs.png")
    print("data/ui/pad_glyphs.png", sheet.size, len(ORDER), "pictures")
