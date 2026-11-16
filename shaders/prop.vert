#version 460 core

// ── Prop vertex shader ────────────────────────────────────
// Loose voxel objects: felled logs, wood on the ground, leaves
// and chips (render/PropRenderer.h). Like a chunk face, but the
// object can be anywhere and turned any way, so:
//   - the texture is laid out from the object's own position
//     (it turns with the object)
//   - the light comes in per vertex, sampled from the world's
//     baked light where that face is now (block RGB, sky),
//     plus how much canopy shades it
// Matches struct PropVertex / PropLight in PropRenderer.h.

layout(location = 0) in vec3  aPos;       // object space (voxels)
layout(location = 1) in uvec4 aFaceTile;  // face, tileX, tileY, glow
layout(location = 2) in vec4  aLight;     // block r, g, b, sky (0..1)
layout(location = 3) in float aShade;     // canopy over it (0..1)

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProjection;
uniform vec3 uCamPos;
uniform int  uFlat;          // 1: one light for the whole object (uFlatLight, uFlatShade)
uniform vec4 uFlatLight;
uniform float uFlatShade;

out      vec2  vFacePos;
flat out vec2  vTile;
flat out float vGlow;
out      vec3  vWorld;
out      vec3  vNormal;
out      float vDist;
out      vec4  vLight;
out      float vShade;

void main() {
    uint face = aFaceTile.x;
    vec3 p = aPos;
    if      (face <= 1u) vFacePos = vec2( p.x,  p.z);
    else if (face == 2u) vFacePos = vec2( p.x, -p.y);
    else if (face == 3u) vFacePos = vec2(-p.x, -p.y);
    else if (face == 4u) vFacePos = vec2( p.z, -p.y);
    else                 vFacePos = vec2(-p.z, -p.y);
    const vec3 NORMALS[6] = vec3[6](vec3(0,1,0), vec3(0,-1,0), vec3(0,0,1), vec3(0,0,-1), vec3(-1,0,0), vec3(1,0,0));
    vNormal = normalize(mat3(uModel) * NORMALS[min(face, 5u)]);
    vTile = vec2(aFaceTile.yz);
    vGlow = float(aFaceTile.w);
    vLight = uFlat == 1 ? uFlatLight : aLight;
    vShade = uFlat == 1 ? uFlatShade : aShade;
    vec4 w = uModel * vec4(p, 1.0);
    vWorld = w.xyz;
    vDist = length(w.xyz - uCamPos);
    gl_Position = uProjection * uView * w;
}
