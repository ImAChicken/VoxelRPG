#version 460 core

// ── Distant terrain vertex shader ─────────────────────────
// See src/render/FarTerrain.h. Positions are world voxels;
// the colour already has the slope shading baked in.
//
// Where the real voxel chunks are drawn (uNearMask, one texel
// per chunk), the distant terrain sinks out of the way, well
// under the voxel ground. Vertices on the edge of that area
// stay put, so the triangles just inside slope down from the
// edge: a curtain that fills any gap between the voxels and
// the distant terrain instead of showing the sky.

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec4 aColor;

uniform mat4 uViewProj;
uniform vec3 uCamPos;
uniform usampler2D uNearMask;
uniform ivec2 uWorldChunks;

out vec3 vColor;
out float vDist;
out vec3 vWorld;
out float vGlow;

const float SINK = 120.0;

bool nearAt(int cx, int cz) {
    if (cx < 0 || cz < 0 || cx >= uWorldChunks.x || cz >= uWorldChunks.y) return false;
    return texelFetch(uNearMask, ivec2(cx, cz), 0).r != 0u;
}

void main() {
    vec3 p = aPos;
    // the chunks this vertex touches (two per axis on a chunk edge)
    int x0 = int(floor((p.x - 0.5) / 128.0)), x1 = int(floor((p.x + 0.5) / 128.0));
    int z0 = int(floor((p.z - 0.5) / 128.0)), z1 = int(floor((p.z + 0.5) / 128.0));
    if (nearAt(x0, z0) && nearAt(x1, z0) && nearAt(x0, z1) && nearAt(x1, z1))
        p.y -= SINK;

    vColor = aColor.rgb;
    vGlow  = aColor.a < 0.5 ? 1.0 : 0.0;      // alpha 0 marks glowing ground (lava)
    vDist  = length(p - uCamPos);
    vWorld = p;
    gl_Position = uViewProj * vec4(p, 1.0);
}
