#version 460 core

// ── The sky world's far view (render/SkyFarView.h) ──────────
// Coarse voxel cells, one face per quad. Where real chunks are drawn
// (uNearMask, one texel per chunk) a cell's faces are thrown off the
// screen: the voxels are there instead.

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec4 aColor;
layout(location = 2) in vec3 aNormal;
layout(location = 3) in ivec2 aChunk;

uniform mat4 uViewProj;
uniform vec3 uCamPos;
uniform usampler2D uNearMask;
uniform ivec2 uWorldChunks;

out vec3 vColor;
out vec3 vNormal;
out vec3 vWorld;
out float vDist;
out float vGlow;

void main() {
    bool near = aChunk.x >= 0 && aChunk.y >= 0 && aChunk.x < uWorldChunks.x && aChunk.y < uWorldChunks.y &&
                texelFetch(uNearMask, aChunk, 0).r != 0u;
    vColor = aColor.rgb;
    vGlow = aColor.a < 0.5 ? 1.0 : 0.0;        // alpha 0: a glowing cell
    vNormal = aNormal;
    vWorld = aPos;
    vDist = length(aPos - uCamPos);
    gl_Position = near ? vec4(2.0, 2.0, 2.0, 1.0) : uViewProj * vec4(aPos, 1.0);
}
