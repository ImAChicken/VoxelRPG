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

// Fake directional light: top brightest, bottom darkest
// Order matches enum Face: +Y, -Y, +Z, -Z, -X, +X
const float FACE_SHADE[6] = float[6](
    1.0, 0.6, 0.8, 0.8, 0.7, 0.7);

void main() {
    vec3 local = vec3(aPosFace.xyz);
    uint face  = aPosFace.w;

    // ── Sprites: two crossed pictures (see Mesher.h sprites()) ──
    if (face >= 6u) {
        local.xz -= 8.0;
        float cu = float(aTile.w & 1u), cv = float((aTile.w >> 1) & 1u);
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
    gl_Position = uProjection * uView * vec4(world, 1.0);
}
