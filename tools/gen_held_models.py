#!/usr/bin/env python3
"""Make the held-item voxel models (data/models/held/*.vxm) for tools and
weapons that are built from simple shapes. Run from the repo root:

    python3 tools/gen_held_models.py

The sword and the axe are hand-made files and aren't touched. Each model
here uses the axe's frame (one hand on the grip, the business end along
+u) and 0.5-inch voxels. Voxel (i, j, k): i along the haft, j toward the
edge, k across the flat side. A haft is 2 x 2 voxels (1 inch) centred on
j = 3.5, k = 1.5.
"""
import math, os

OUT = "data/models/held"
WOOD, DARKWOOD, IRON, BRIGHT, LEATHER, STONE, BRASS = 1, 2, 3, 4, 5, 6, 7
PALETTE = {
    WOOD: (138, 92, 56), DARKWOOD: (78, 54, 40), IRON: (118, 122, 130),
    BRIGHT: (214, 220, 226), LEATHER: (95, 70, 54), STONE: (128, 124, 118),
    BRASS: (191, 164, 102),
}
CJ, CK = 3.5, 1.5          # the haft's centre line


class Model:
    def __init__(self, title, grip_back=3.0):
        self.title = title
        self.vox = {}
        self.grip_back = grip_back      # inches of haft behind the fist (origin u)
        self.palette = dict(PALETTE)
        self.points = []
        self.thrust = None              # where it points at the height of a thrust (wrist frame)

    def put(self, i, j, k, c):
        self.vox[(int(i), int(j), int(k))] = c

    def rod(self, i0, i1, r, c, r_end=None):
        """A round rod from i0 to i1 (inclusive), radius r voxels (r_end at i1)."""
        for i in range(i0, i1 + 1):
            t = (i - i0) / max(1, i1 - i0)
            rr = r if r_end is None else r + (r_end - r) * t
            R = int(math.ceil(rr)) + 1
            for j in range(int(CJ) - R, int(CJ) + R + 2):
                for k in range(int(CK) - R, int(CK) + R + 2):
                    if (j - CJ) ** 2 + (k - CK) ** 2 <= rr * rr + 0.01:
                        self.put(i, j, k, c)

    def plate(self, i0, i1, j_of_i, k0, k1, c):
        """A flat plate: for each i in i0..i1, j from j_of_i(i) = (lo, hi), k0..k1."""
        for i in range(i0, i1 + 1):
            span = j_of_i(i)
            if not span:
                continue
            lo, hi = span
            for j in range(int(math.floor(lo)), int(math.ceil(hi)) + 1):
                for k in range(k0, k1 + 1):
                    self.put(i, j, k, c)

    def write(self, name, comment):
        lo_i = min(p[0] for p in self.vox)
        assert lo_i >= 0
        # voxel indices must be >= 0: shift j / k up and the origin down to match
        sj = -min(0, min(p[1] for p in self.vox))
        sk = -min(0, min(p[2] for p in self.vox))
        if sj or sk:
            self.vox = {(i, j + sj, k + sk): c for (i, j, k), c in self.vox.items()}
        lines = [f"# {self.title}", f"# {comment}",
                 "# Made by tools/gen_held_models.py: edit that and run it again rather than this file.",
                 "vxm 1", "voxel 0.5", f"origin {-self.grip_back:g} {-2 - sj * 0.5:g} {-1 - sk * 0.5:g}",
                 "along 0 -0.5 1", "edge 0 -1 -0.5", "across 1 0 0"]
        if self.thrust:
            lines.append("thrust " + " ".join(f"{x:g}" for x in self.thrust))
        used = sorted(set(self.vox.values()))
        for c in used:
            r, g, b = self.palette[c]
            lines.append(f"color {c} {r} {g} {b}")
        for p in self.points:
            lines.append("point " + " ".join(f"{x:g}" if not isinstance(x, str) else x for x in p))
        for (i, j, k), c in sorted(self.vox.items()):
            lines.append(f"v {i} {j} {k} {c}")
        with open(os.path.join(OUT, name + ".vxm"), "w") as f:
            f.write("\n".join(lines) + "\n")
        print(f"{name}.vxm  {len(self.vox)} voxels")


def pickaxe(name, head, tip, title):
    m = Model(title)
    m.rod(0, 51, 1.0, WOOD)                       # 26-inch haft
    m.rod(0, 4, 1.15, LEATHER)                    # grip end
    for d in range(-15, 16):                      # the head: a curved pick both ways
        a = abs(d) / 15.0
        drop = int(round(5 * a * a))              # curves down toward the hand
        h = max(1, int(round(4 - 3 * a)))         # thick at the eye, thin at the points
        for i in range(46 - drop, 46 - drop + h):
            for k in range(1 if a > 0.6 else 0, 3 if a > 0.6 else 4):
                m.put(i, CJ - 0.5 + d, k, tip if a > 0.8 else head)
    m.write(name, "Pickaxe: a 26-inch haft and a two-pointed head")


def shovel(name, blade, title):
    m = Model(title)
    m.rod(0, 55, 1.0, WOOD)
    m.rod(0, 4, 1.15, LEATHER)
    m.rod(52, 57, 1.3, IRON if blade == IRON else DARKWOOD)   # the socket
    def span(i):
        t = (i - 56) / 18.0                       # 9-inch blade, rounded at the end
        w = 7.0 if t < 0.6 else 7.0 * math.sqrt(max(0.0, 1 - ((t - 0.6) / 0.42) ** 2))
        return None if w < 0.5 else (CJ - w, CJ + w)
    m.plate(56, 74, span, 1, 2, blade)
    m.write(name, "Shovel: a long haft and a rounded blade")


def hoe():
    m = Model("Hoe: a long haft with a flat blade bent toward you")
    m.rod(0, 59, 1.0, WOOD)
    m.rod(0, 4, 1.15, LEATHER)
    for j in range(4, 15):                        # the neck, out from the haft
        for i in range(57, 60):
            m.put(i, j, 1, IRON); m.put(i, j, 2, IRON)
    m.plate(46, 59, lambda i: (13, 17), 1, 2, IRON)   # the blade, back down the haft
    m.write("hoe", "Hoe for tilling")


def builders_hammer():
    m = Model("Builder's hammer: a short haft and an iron head with a claw")
    m.rod(0, 29, 1.0, WOOD)
    m.rod(0, 6, 1.15, LEATHER)
    for i in range(27, 35):                       # the head: 4 in tall, 2 in thick
        for j in range(-3, 8):                    # striking face on +j, claw on -j
            for k in range(0, 4):
                m.put(i, j, k, BRIGHT if j == 7 else IRON)
    for j in range(-8, -3):                       # the claw: two prongs curving down
        drop = (-3 - j) // 2
        for k in (0, 3):
            m.put(30 - drop, j, k, IRON); m.put(31 - drop, j, k, IRON)
    m.write("builders_hammer", "Builder's Hammer")


def club():
    m = Model("Wooden club: swells from the grip to a studded head")
    m.rod(0, 51, 1.0, WOOD, r_end=4.8)
    m.rod(0, 10, 1.2, LEATHER)
    for i in (36, 42, 48):                        # iron studs round the head
        t = i / 51.0
        r = 1.0 + 3.8 * t + 0.6
        for a in range(0, 360, 60):
            j = CJ + r * math.cos(math.radians(a + i * 7))
            k = CK + r * math.sin(math.radians(a + i * 7))
            m.put(i, round(j - 0.5), round(k - 0.5), IRON)
    m.write("club", "Club")


def spear():
    m = Model("Spear: a 6-foot ash shaft and a leaf-shaped iron head", grip_back=24)
    # thrusts (movesets.toml grip = "thrust") turn it to point along the
    # forearm, which at the end of Punch_Cross points straight ahead
    m.thrust = (0.10, -0.97, -0.23)
    m.rod(0, 121, 1.0, WOOD)
    m.rod(0, 3, 1.3, IRON)                        # butt cap
    m.rod(44, 54, 1.2, LEATHER)                   # where the fist holds it
    m.rod(116, 123, 1.4, IRON)                    # socket
    def leaf(i):
        t = (i - 122) / 22.0                      # 11-inch head
        w = 3.6 * math.sin(math.pi * min(1.0, t * 1.25)) * (1 - t) ** 0.35 if t < 1 else 0
        return (CJ - w, CJ + w) if t <= 1 else None
    m.plate(122, 144, leaf, 1, 2, IRON)
    m.plate(122, 142, lambda i: (CJ - 0.4, CJ + 0.4), 1, 2, BRIGHT)   # the midrib
    m.write("spear", "Spear")


def staff():
    m = Model("Quarterstaff: a 5-and-a-half-foot pole, iron-capped, held a third of the way up", grip_back=18)
    m.rod(0, 131, 1.05, WOOD)
    m.rod(0, 5, 1.45, IRON)                       # caps
    m.rod(126, 131, 1.45, IRON)
    m.rod(30, 42, 1.25, LEATHER)                  # grips: where the fist holds it, and higher up
    m.rod(70, 80, 1.25, LEATHER)
    m.write("staff", "Quarterstaff")


def dev_wand(name, gem, title):
    m = Model(title)
    m.palette[9] = gem
    m.rod(0, 27, 0.8, DARKWOOD)
    m.rod(25, 28, 1.3, BRASS)
    for i in range(29, 35):                       # the gem: a little diamond
        r = 2.6 - abs(i - 31.5) * 0.75
        for j in range(-2, 9):
            for k in range(-3, 6):
                if abs(j - CJ) + abs(k - CK) <= r:
                    m.put(i, j, k, 9)
    m.write(name, "Dev tool wand (Test Play only)")


def spellbook(name, cover, gem, title):
    """A book held by its spine's foot: 8 x 6 inches, 1.5 thick. i runs up
    the spine, j across the cover, k through the pages."""
    m = Model(title, grip_back=1.0)
    m.palette[8] = cover
    m.palette[9] = gem
    PAGES = 10
    for i in range(0, 16):
        for j in range(-1, 12):
            for k in range(-1, 3):
                edge = j == 11 or i in (0, 15)
                cov = k in (-1, 2) or j == -1
                if cov:
                    m.put(i, j, k, BRASS if (cov and (i in (0, 15)) and j in (10, 11)) else 8)   # cover, brass corners
                elif edge:
                    m.put(i, j, k, BRIGHT)               # the page edges
                else:
                    m.put(i, j, k, PAGES)
    for i in range(6, 10):                               # a gem on the front cover
        for j in range(4, 8):
            if abs(i - 7.5) + abs(j - 5.5) <= 2.2:
                m.put(i, j, 3, 9)
    m.palette[PAGES] = (226, 216, 190)
    m.write(name, "Spellbook: held by the spine; the gem on its cover shows the spell")


if __name__ == "__main__":
    os.makedirs(OUT, exist_ok=True)
    pickaxe("wooden_pickaxe", DARKWOOD, DARKWOOD, "Wooden pickaxe")
    pickaxe("stone_pickaxe", STONE, STONE, "Stone pickaxe")
    pickaxe("iron_pickaxe", IRON, BRIGHT, "Iron pickaxe")
    shovel("wooden_shovel", WOOD, "Wooden shovel")
    shovel("iron_shovel", IRON, "Iron shovel")
    hoe()
    builders_hammer()
    club()
    spear()
    staff()
    for name, gem in [("dev_wand", (90, 200, 255)), ("dev_fill", (90, 220, 120)), ("dev_replace", (240, 200, 70)),
                      ("dev_brush", (230, 120, 220)), ("dev_copy", (120, 150, 255)), ("dev_paste", (150, 120, 255)),
                      ("dev_explode", (255, 110, 60)), ("dev_breaker", (220, 70, 70)), ("dev_cube", (240, 240, 240))]:
        dev_wand(name, gem, "Dev tool: " + name)
    spellbook("spellbook_wind", (70, 120, 140), (170, 235, 245), "Windburst spellbook")
    spellbook("spellbook_ice", (52, 78, 132), (150, 205, 255), "Ice spike spellbook")
