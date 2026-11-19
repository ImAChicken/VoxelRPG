#version 460 core

// ── Roof vertex shader ────────────────────────────────────
// A roof piece drawn as one sloped surface (render/RoofRenderer.h).
// Positions come in world space; uv is in voxels (along the eave,
// up the slope, from the world so neighbouring pieces line up); the
// light per vertex like a prop (block RGB, sky, canopy shade).
// Matches struct RoofRenderer::Vert.

layout(location = 0) in vec3  aPos;
layout(location = 1) in vec2  aUV;
layout(location = 2) in vec3  aNormal;
layout(location = 3) in uvec4 aTile;     // tileX, tileY, style (0 tiles, 1 slate, 2 thatch, 3 trim), -
layout(location = 4) in vec4  aLight;
layout(location = 5) in float aShade;

uniform mat4 uView;
uniform mat4 uProjection;
uniform vec3 uCamPos;

out      vec2  vUV;
flat out vec2  vTile;
flat out uint  vStyle;
out      vec3  vWorld;
out      vec3  vNormal;
out      float vDist;
out      vec4  vLight;
out      float vShade;

void main() {
    vUV = aUV;
    vTile = vec2(aTile.xy);
    vStyle = aTile.z;
    vNormal = aNormal;
    vLight = aLight;
    vShade = aShade;
    vWorld = aPos;
    vDist = length(aPos - uCamPos);
    gl_Position = uProjection * uView * vec4(aPos, 1.0);
}
