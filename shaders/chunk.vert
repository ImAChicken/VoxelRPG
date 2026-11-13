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

out      vec2  vFacePos;       // position across the face, in voxels
flat out vec2  vTile;          // atlas tile (column, row)
flat out float vShade;

// Fake directional light: top brightest, bottom darkest
// Order matches enum Face: +Y, -Y, +Z, -Z, -X, +X
const float FACE_SHADE[6] = float[6](
    1.0, 0.6, 0.8, 0.8, 0.7, 0.7);

void main() {
    vec3 local = vec3(aPosFace.xyz);
    uint face  = aPosFace.w;

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

    gl_Position = uProjection * uView *
                  vec4(local + uChunkOrigin, 1.0);
}
