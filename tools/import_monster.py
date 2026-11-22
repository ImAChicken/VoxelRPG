#!/usr/bin/env python3
"""Turns a rigged, animated glTF monster (the Quaternius monster kit's
glTF folders) into the game's monster file (data/models/monsters/
<name>.vmon), read by src/character/MonsterModel.h and drawn with the
characters' toon shading and edge lines (render/CharacterRenderer.h).

    python3 tools/import_monster.py <file.gltf> [more.gltf ...] [--out data/models/monsters]

What it keeps:
  - the mesh as flat-shaded triangles (one normal per face, like our
    faceted bodies), each face one colour: the texture's colour at the
    face's middle (the kit's texture is a sheet of flat swatches, so
    nothing is lost)
  - the skin: up to 4 bones a vertex, their weights
  - the skeleton: each bone's parent, rest pose and inverse bind matrix
    (a root bone's matrix includes the nodes above it, the armature)
  - every animation, resampled at 30 frames a second: each bone's
    position, rotation (a quaternion) and scale per frame

The file (little-endian):
  "VMON", u32 version (1)
  u32 bones; per bone: u8 name length, name, i32 parent,
      f32[16] base (column-major: the nodes above a root bone; identity
      for the rest), f32 rest T[3] R[4] (x y z w) S[3], f32[16] inverse bind
  u32 vertices (3 a triangle, in order); per vertex: f32 pos[3],
      i8 normal[3], u8 0, u8 rgb[3], u8 0, u8 bones[4], u8 weights[4]
  f32 min[3], max[3]   (the mesh at rest)
  u32 clips; per clip: u8 name length, name, f32 seconds, f32 fps,
      u32 frames, then frames × bones × (T[3] R[4] S[3]) f32
(Claude, 2026-11-22, for the mushroom monsters; the kit is CC0.)
"""
import base64, io, json, math, os, struct, sys
from PIL import Image

FPS = 30.0
COMP = {5120: ('b', 1), 5121: ('B', 1), 5122: ('h', 2), 5123: ('H', 2), 5125: ('I', 4), 5126: ('f', 4)}
NCOMP = {'SCALAR': 1, 'VEC2': 2, 'VEC3': 3, 'VEC4': 4, 'MAT4': 16}


def load_buffers(g, base):
    out = []
    for b in g['buffers']:
        uri = b['uri']
        if uri.startswith('data:'):
            out.append(base64.b64decode(uri.split(',', 1)[1]))
        else:
            out.append(open(os.path.join(base, uri), 'rb').read())
    return out


def accessor(g, bufs, i):
    a = g['accessors'][i]
    v = g['bufferViews'][a['bufferView']]
    fmt, size = COMP[a['componentType']]
    n = NCOMP[a['type']]
    stride = v.get('byteStride', size * n)
    off = v.get('byteOffset', 0) + a.get('byteOffset', 0)
    data = bufs[v['buffer']]
    norm = a.get('normalized', False)
    res = []
    for k in range(a['count']):
        vals = struct.unpack_from('<' + fmt * n, data, off + k * stride)
        if norm and fmt in 'BH':
            vals = tuple(x / (255.0 if fmt == 'B' else 65535.0) for x in vals)
        res.append(vals if n > 1 else vals[0])
    return res


# ── small maths (column-major 4x4 as lists of 16) ─────────
def mat_mul(a, b):
    r = [0.0] * 16
    for c in range(4):
        for rr in range(4):
            r[c * 4 + rr] = sum(a[k * 4 + rr] * b[c * 4 + k] for k in range(4))
    return r


def trs(t, q, s):
    x, y, z, w = q
    m = [1 - 2 * (y * y + z * z), 2 * (x * y + z * w), 2 * (x * z - y * w), 0,
         2 * (x * y - z * w), 1 - 2 * (x * x + z * z), 2 * (y * z + x * w), 0,
         2 * (x * z + y * w), 2 * (y * z - x * w), 1 - 2 * (x * x + y * y), 0,
         0, 0, 0, 1]
    for c in range(3):
        for rr in range(3):
            m[c * 4 + rr] *= s[c]
    m[12], m[13], m[14] = t
    return m


IDENT = [1.0, 0, 0, 0, 0, 1.0, 0, 0, 0, 0, 1.0, 0, 0, 0, 0, 1.0]


def node_local(n):
    if 'matrix' in n:
        return list(n['matrix'])
    return trs(n.get('translation', [0, 0, 0]), n.get('rotation', [0, 0, 0, 1]), n.get('scale', [1, 1, 1]))


def node_trs(n):
    return (list(n.get('translation', [0, 0, 0])), list(n.get('rotation', [0, 0, 0, 1])), list(n.get('scale', [1, 1, 1])))


def apply(m, p):
    return tuple(m[0 + r] * p[0] + m[4 + r] * p[1] + m[8 + r] * p[2] + m[12 + r] for r in range(3))


def slerp(a, b, t):
    d = sum(x * y for x, y in zip(a, b))
    if d < 0:
        b = [-x for x in b]
        d = -d
    if d > 0.9995:
        r = [x + (y - x) * t for x, y in zip(a, b)]
    else:
        th = math.acos(min(1.0, d))
        s = math.sin(th)
        wa, wb = math.sin((1 - t) * th) / s, math.sin(t * th) / s
        r = [wa * x + wb * y for x, y in zip(a, b)]
    l = math.sqrt(sum(x * x for x in r)) or 1.0
    return [x / l for x in r]


def sample(times, values, interp, t, path):
    """a channel's value at time t (values: per key; CUBICSPLINE keeps the middle one)"""
    if interp == 'CUBICSPLINE':
        values = values[1::3]
    if t <= times[0]:
        return list(values[0])
    if t >= times[-1]:
        return list(values[-1])
    lo, hi = 0, len(times) - 1
    while hi - lo > 1:
        mid = (lo + hi) // 2
        if times[mid] <= t:
            lo = mid
        else:
            hi = mid
    if interp == 'STEP':
        return list(values[lo])
    f = (t - times[lo]) / max(times[hi] - times[lo], 1e-9)
    if path == 'rotation':
        return slerp(values[lo], values[hi], f)
    return [x + (y - x) * f for x, y in zip(values[lo], values[hi])]


def convert(path, outdir):
    g = json.load(open(path))
    bufs = load_buffers(g, os.path.dirname(path))
    nodes = g['nodes']
    parent_of = {}
    for i, n in enumerate(nodes):
        for c in n.get('children', []):
            parent_of[c] = i
    skin = g['skins'][0]
    joints = skin['joints']
    jindex = {nd: k for k, nd in enumerate(joints)}
    ibm = accessor(g, bufs, skin['inverseBindMatrices']) if 'inverseBindMatrices' in skin else [IDENT] * len(joints)

    def world_of(nd):
        m = IDENT
        while nd is not None:
            m = mat_mul(node_local(nodes[nd]), m)
            nd = parent_of.get(nd)
        return m

    bones = []
    for k, nd in enumerate(joints):
        p = parent_of.get(nd)
        par = jindex.get(p, -1) if p is not None else -1
        base = IDENT if par >= 0 else (world_of(p) if p is not None else IDENT)
        t, q, s = node_trs(nodes[nd])
        bones.append((nodes[nd].get('name', 'bone%d' % k), par, base, t, q, s, list(ibm[k])))
    # parents first (the game walks the list in order)
    order, placed = [], set()
    while len(order) < len(bones):
        moved = False
        for k, b in enumerate(bones):
            if k in placed:
                continue
            if b[1] < 0 or b[1] in placed:
                order.append(k)
                placed.add(k)
                moved = True
        if not moved:
            raise SystemExit(path + ': the skeleton loops')
    remap = {old: new for new, old in enumerate(order)}
    bones = [bones[k] for k in order]
    bones = [(n, remap[p] if p >= 0 else -1, base, t, q, s, ib) for (n, p, base, t, q, s, ib) in bones]

    # the texture (the kit's colour sheet)
    tex = None
    mats = g.get('materials', [])
    try:
        ti = mats[0]['pbrMetallicRoughness']['baseColorTexture']['index']
        img = g['images'][g['textures'][ti]['source']]
        if 'uri' in img and not img['uri'].startswith('data:'):
            tex = Image.open(os.path.join(os.path.dirname(path), img['uri'])).convert('RGB')
        elif 'uri' in img:
            tex = Image.open(io.BytesIO(base64.b64decode(img['uri'].split(',', 1)[1]))).convert('RGB')
        else:                                   # packed in the file's buffer
            v = g['bufferViews'][img['bufferView']]
            raw = bufs[v['buffer']][v.get('byteOffset', 0):v.get('byteOffset', 0) + v['byteLength']]
            tex = Image.open(io.BytesIO(raw)).convert('RGB')
    except (KeyError, IndexError, FileNotFoundError):
        print(path + ': no colour texture found; faces take the material colour')

    def colour(mi, uv):
        if tex is not None and uv is not None:
            x = min(tex.width - 1, max(0, int(uv[0] % 1.0 * tex.width)))
            y = min(tex.height - 1, max(0, int(uv[1] % 1.0 * tex.height)))
            return tex.getpixel((x, y))
        f = (mats[mi].get('pbrMetallicRoughness', {}).get('baseColorFactor') if mi is not None and mi < len(mats) else None) or [0.8, 0.8, 0.8, 1]
        return tuple(int(max(0, min(1, c)) ** (1 / 2.2) * 255) for c in f[:3])

    verts = []
    lo, hi = [1e9] * 3, [-1e9] * 3
    for ni, n in enumerate(nodes):
        if 'mesh' not in n:
            continue
        skinned = 'skin' in n
        mesh_m = world_of(ni)
        for prim in g['meshes'][n['mesh']]['primitives']:
            if prim.get('mode', 4) != 4:
                continue
            A = prim['attributes']
            P = accessor(g, bufs, A['POSITION'])
            UV = accessor(g, bufs, A['TEXCOORD_0']) if 'TEXCOORD_0' in A else None
            J = accessor(g, bufs, A['JOINTS_0']) if skinned and 'JOINTS_0' in A else None
            W = accessor(g, bufs, A['WEIGHTS_0']) if skinned and 'WEIGHTS_0' in A else None
            idx = accessor(g, bufs, prim['indices']) if 'indices' in prim else list(range(len(P)))
            # an unskinned mesh rides the bone it hangs under, if any
            rigid = None
            if not skinned:
                p = parent_of.get(ni)
                while p is not None and p not in jindex:
                    p = parent_of.get(p)
                rigid = remap[jindex[p]] if p is not None else 0
            for t0 in range(0, len(idx) - 2, 3):
                tri = idx[t0:t0 + 3]
                ps = [P[i] if skinned else apply(mesh_m, P[i]) for i in tri]
                e1 = [ps[1][c] - ps[0][c] for c in range(3)]
                e2 = [ps[2][c] - ps[0][c] for c in range(3)]
                nrm = [e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0]]
                l = math.sqrt(sum(x * x for x in nrm))
                if l < 1e-12:
                    continue
                nrm = [x / l for x in nrm]
                uv = None
                if UV:
                    uv = [sum(UV[i][c] for i in tri) / 3.0 for c in range(2)]
                rgb = colour(prim.get('material'), uv)
                for k, i in enumerate(tri):
                    if skinned and J and W:
                        js = [remap[jindex[joints[j]]] if j < len(joints) else 0 for j in J[i]]
                        ws = list(W[i])
                    else:
                        js, ws = [rigid, 0, 0, 0], [1.0, 0, 0, 0]
                    tot = sum(ws) or 1.0
                    wb = [int(round(w / tot * 255)) for w in ws]
                    wb[max(range(4), key=lambda q: wb[q])] += 255 - sum(wb)
                    p = ps[k]
                    for c in range(3):
                        lo[c] = min(lo[c], p[c])
                        hi[c] = max(hi[c], p[c])
                    verts.append((p, nrm, rgb, js, wb))

    # animations, resampled
    clips = []
    for a in g.get('animations', []):
        chans = {}
        dur = 0.0
        for ch in a['channels']:
            nd = ch['target'].get('node')
            if nd not in jindex:
                continue
            s = a['samplers'][ch['sampler']]
            times = accessor(g, bufs, s['input'])
            vals = accessor(g, bufs, s['output'])
            dur = max(dur, times[-1])
            chans[(remap[jindex[nd]], ch['target']['path'])] = (times, vals, s.get('interpolation', 'LINEAR'))
        frames = max(2, int(math.ceil(dur * FPS)) + 1)
        data = []
        for f in range(frames):
            t = min(dur, f / FPS)
            for b, bone in enumerate(bones):
                T, R, S = list(bone[3]), list(bone[4]), list(bone[5])
                if (b, 'translation') in chans:
                    T = sample(*chans[(b, 'translation')][:3], t, 'translation')
                if (b, 'rotation') in chans:
                    R = sample(*chans[(b, 'rotation')][:3], t, 'rotation')
                if (b, 'scale') in chans:
                    S = sample(*chans[(b, 'scale')][:3], t, 'scale')
                data.extend(T + R + S)
        clips.append((a.get('name', 'clip%d' % len(clips)), dur, frames, data))

    name = os.path.splitext(os.path.basename(path))[0].lower()
    os.makedirs(outdir, exist_ok=True)
    out = os.path.join(outdir, name + '.vmon')
    with open(out, 'wb') as f:
        f.write(b'VMON' + struct.pack('<I', 1))
        f.write(struct.pack('<I', len(bones)))
        for (n, p, base, t, q, s, ib) in bones:
            nb = n.encode()[:255]
            f.write(struct.pack('<B', len(nb)) + nb + struct.pack('<i', p))
            f.write(struct.pack('<16f', *base))
            f.write(struct.pack('<10f', *(list(t) + list(q) + list(s))))
            f.write(struct.pack('<16f', *ib))
        f.write(struct.pack('<I', len(verts)))
        for (p, nrm, rgb, js, wb) in verts:
            f.write(struct.pack('<3f', *p))
            f.write(struct.pack('<3bB', *[max(-127, min(127, int(round(x * 127)))) for x in nrm], 0))
            f.write(struct.pack('<4B', rgb[0], rgb[1], rgb[2], 0))
            f.write(struct.pack('<4B', *js))
            f.write(struct.pack('<4B', *wb))
        f.write(struct.pack('<6f', *(lo + hi)))
        f.write(struct.pack('<I', len(clips)))
        for (n, dur, frames, data) in clips:
            nb = n.encode()[:255]
            f.write(struct.pack('<B', len(nb)) + nb)
            f.write(struct.pack('<ffI', dur, FPS, frames))
            f.write(struct.pack('<%df' % len(data), *data))
    print('%s: %d bones, %d triangles, %d clips (%s), %.1f x %.1f x %.1f -> %s' % (
        os.path.basename(path), len(bones), len(verts) // 3, len(clips), ', '.join(c[0] for c in clips),
        hi[0] - lo[0], hi[1] - lo[1], hi[2] - lo[2], out))


if __name__ == '__main__':
    args = sys.argv[1:]
    outdir = 'data/models/monsters'
    if '--out' in args:
        i = args.index('--out')
        outdir = args[i + 1]
        del args[i:i + 2]
    if not args:
        print(__doc__)
        sys.exit(1)
    for p in args:
        convert(p, outdir)
