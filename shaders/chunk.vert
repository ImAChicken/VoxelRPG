#version 460 core

// ── Chunk vertex shader ───────────────────────────────────
// Vertices are 8 bytes: position in voxels relative to the
// chunk corner, the face direction, and the atlas tile.
// Matches struct ChunkVertex in src/render/Mesher.h.

layout(location = 0) in uvec4 aPosFace;   // x, y, z, face
layout(location = 1) in uvec4 aTile;      // tileX, tileY, -, -

uniform mat4 uView;
uniform mat4 uProjection;
uniform vec3 uChunkOrigin;     // world position of the chunk corner
uniform vec3 uCamPos;          // for the distance haze
// Plants (Settings: Plants): 0 plain crossed pictures, 1 natural:
// each plant a little off its grid spot, lit as a rounded clump,
// swaying in the wind (render/Wind.h: uWind = way x, z, strength)
uniform int   uPlantStyle;
uniform float uTime;
uniform vec3  uWind;
uniform sampler3D  uLight;     // the section's baked light (sky in A): open ground feels the wind
uniform usampler2D uCanopy;    // leaves above each column: a forest floor is sheltered

out      vec2  vFacePos;       // position across the face, in voxels
flat out vec2  vTile;          // atlas tile (column, row)
flat out float vShade;
out      float vDist;          // distance from the camera
out      vec3  vLocal;         // position in the section, voxels (for the light texture)
out      vec3  vWorld;
flat out vec3  vNormal;
flat out float vGlow;          // 1 for blocks that give off light: drawn at full brightness
out      vec2  vSpriteUV;      // sprites: position in the picture, in tiles (0 = top left)
flat out float vSprite;        // 1 for sprite quads (flowers, reeds)
out      vec3  vSpriteNormal;  // sprites: the rounded clump's normal here
out      float vSpriteLow;     // sprites: 0 at the ground .. 1 at the top (the base is shaded)
flat out float vPane;          // 1 for window glass sheets (Mesher.h panes)

float hash12(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

// Fake directional light: top brightest, bottom darkest
// Order matches enum Face: +Y, -Y, +Z, -Z, -X, +X
const float FACE_SHADE[6] = float[6](
    1.0, 0.6, 0.8, 0.8, 0.7, 0.7);

void main() {
    vec3 local = vec3(aPosFace.xyz);
    uint face  = aPosFace.w;

    vPane = 0.0;
    // ── Panes: one sheet through the middle of the voxels (Mesher.h panes()) ──
    if (face >= 8u) {
        vec3 n;
        if (face == 8u) { local.z += 0.5; vFacePos = vec2(local.x, -local.y); n = vec3(0, 0, 1); }
        else            { local.x += 0.5; vFacePos = vec2(local.z, -local.y); n = vec3(1, 0, 0); }
        vPane = 1.0;
        vSprite = 0.0;
        vSpriteUV = vec2(0.0);
        vSpriteNormal = n;
        vSpriteLow = 1.0;
        vTile  = vec2(aTile.xy);
        vShade = 0.8;
        vGlow  = float(aTile.z & 1u);
        vNormal = n;
        vLocal = local;
        vec3 w = local + uChunkOrigin;
        vWorld = w;
        vDist  = length(w - uCamPos);
        gl_Position = uProjection * uView * vec4(w, 1.0);
        return;
    }
    // ── Sprites: two crossed pictures (see Mesher.h sprites()) ──
    if (face >= 6u) {
        local.xz -= 8.0;
        float cu = float(aTile.w & 1u), cv = float((aTile.w >> 1) & 1u);
        float rawCu = cu;
        vec3 n = vec3(0, 1, 0);
        vSpriteLow = cv;
        if (uPlantStyle == 1) {
            // the plant's middle: the corner, back by half its footprint
            float halfW = float((aTile.z >> 2) & 3u) + 0.5;
            vec2 sgn = face == 6u ? vec2(rawCu > 0.5 ? -1.0 : 1.0) : vec2(rawCu > 0.5 ? -1.0 : 1.0, rawCu > 0.5 ? 1.0 : -1.0);
            vec2 mid = local.xz + sgn * halfW;
            vec2 wmid = mid + uChunkOrigin.xz;
            // off its grid spot, the whole plant together
            vec2 jit = vec2(hash12(wmid), hash12(wmid + 17.3)) - 0.5;
            local.xz += jit * 0.8;
            // a rounded clump: the normal leans out toward each edge
            vec2 along = (face == 6u ? vec2(1.0, 1.0) : vec2(1.0, -1.0)) * 0.7071;
            float across = rawCu * 2.0 - 1.0;
            n = normalize(vec3(along.x * across * 0.9, 1.0, along.y * across * 0.9));
            // wind: the root stays put and each corner bends by its height
            // above the root (Mesher.h pad0 bits 4-7), more than in
            // proportion, so a stem and the flower on it lean as one and
            // the tips move most; open sky (baked sky light) only, not
            // under leaves (canopy), stronger up high; gusts roll across
            // the land downwind; each plant its own phase
            float rootH = float((aTile.z >> 4) & 15u);
            if (rootH > 0.0) {
                vec3 lp = vec3(mid.x, local.y - 0.5, mid.y);
                float sky = texture(uLight, (lp * 0.5 + 1.0) / 66.0).a;
                uvec3 cn = texelFetch(uCanopy, clamp(ivec2(floor(mid * 0.5)) + 1, ivec2(0), textureSize(uCanopy, 0) - 1), 0).rgb;
                float shelter = 0.0;
                if (cn.r > 0u && lp.y * 0.5 + uChunkOrigin.y * 0.5 < float(cn.b) + 1.0)
                    shelter = clamp(float(cn.r) / 3.0, 0.0, 0.85);
                float high = clamp((uChunkOrigin.y + local.y - 150.0) / 140.0, 0.45, 1.35);
                float open = sky * sky * (1.0 - shelter) * high;
                vec2 wd = uWind.xy;
                float along2 = dot(wmid, wd);
                float gust = 0.5 + 0.5 * sin(along2 * 0.045 - uTime * 1.6) * (0.6 + 0.4 * sin(along2 * 0.013 + wmid.x * 0.007 - uTime * 0.4));
                float phase = hash12(wmid + 3.1) * 6.2832;
                float flutter = 0.65 + 0.35 * sin(uTime * (2.2 + uWind.z * 1.8) + phase);
                float push = uWind.z * open * (0.25 + 0.75 * gust) * flutter * 0.9;
                float bend = min(pow(rootH / 2.0, 1.3), 3.0);    // 1 voxel up: 0.4, 2: 1, 4: 2.5
                local.xz += wd * push * bend;
                local.y  -= (push * bend) * (push * bend) * 0.15;   // bends over, a little lower
            }
        }
        vSpriteNormal = n;
        float tw = float(((aTile.w >> 2) & 3u) + 1u), th = float(((aTile.w >> 4) & 7u) + 1u);
        if ((aTile.z & 2u) != 0u) cu = 1.0 - cu;
        vSpriteUV = vec2(mix(0.01, tw - 0.01, cu), mix(0.01, th - 0.01, 1.0 - cv));   // never touch the next tile
        vSprite   = 1.0;
        vFacePos  = vec2(0.0);
        vTile     = vec2(aTile.xy);
        vShade    = 0.9;
        vGlow     = float(aTile.z & 1u);
        vNormal   = vec3(0, 1, 0);       // lit like the ground it grows from
        vLocal    = local;
        vec3 w = local + uChunkOrigin;
        vWorld = w;
        vDist  = length(w - uCamPos);
        gl_Position = uProjection * uView * vec4(w, 1.0);
        return;
    }
    vSprite = 0.0;
    vSpriteUV = vec2(0.0);
    vSpriteNormal = vec3(0, 1, 0);
    vSpriteLow = 1.0;

    // Lay the texture across the face using the vertex
    // position itself, so it lines up from voxel to voxel
    // and from chunk to chunk no matter how big the quad is.
    // The signs keep side textures upright and unmirrored.
    if      (face <= 1u) vFacePos = vec2( local.x,  local.z);   // top / bottom
    else if (face == 2u) vFacePos = vec2( local.x, -local.y);   // +Z
    else if (face == 3u) vFacePos = vec2(-local.x, -local.y);   // -Z
    else if (face == 4u) vFacePos = vec2( local.z, -local.y);   // -X
    else                 vFacePos = vec2(-local.z, -local.y);   // +X

    vTile  = vec2(aTile.xy);
    vShade = FACE_SHADE[min(face, 5u)];
    vGlow  = float(aTile.z & 1u);
    const vec3 NORMALS[6] = vec3[6](vec3(0,1,0), vec3(0,-1,0), vec3(0,0,1), vec3(0,0,-1), vec3(-1,0,0), vec3(1,0,0));
    vNormal = NORMALS[min(face, 5u)];
    vLocal = local;

    vec3 world = local + uChunkOrigin;
    vWorld = world;
    vDist = length(world - uCamPos);
    // A hair outward along the quad's edges (Mesher.h emitQuad: which
    // way in aTile.w), more with distance: seals the pinholes where
    // merged quads meet, which otherwise show the sky through walls
    uint e = aTile.w;
    vec3 grow = vec3(float(e & 1u) * (float((e >> 1) & 1u) * 2.0 - 1.0),
                     float((e >> 2) & 1u) * (float((e >> 3) & 1u) * 2.0 - 1.0),
                     float((e >> 4) & 1u) * (float((e >> 5) & 1u) * 2.0 - 1.0));
    world += grow * (0.002 + vDist * 0.0007);
    gl_Position = uProjection * uView * vec4(world, 1.0);
}
