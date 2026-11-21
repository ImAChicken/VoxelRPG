#version 460 core

// ── Distant terrain fragment shader ───────────────────────
// Lit by the sky of the moment and hazed exactly like the voxel
// chunks (shaders/chunk.frag), so the two meet without a seam.

in vec3 vColor;
in float vDist;
in vec3 vWorld;
in float vGlow;

out vec4 outColor;

uniform vec3  uCamPos;
uniform vec3  uSkyLight, uAmbient, uFogHorizon, uFogSunGlow, uSunDir;
uniform float uFogDensity, uSeamStart, uSeamEnd, uSeamHaze, uNight;
uniform vec3  uLightDir;
#include "terrainlight.glsl"

// Haze at the voxel / far-terrain seam: measured along the square
// of voxel chunks (the larger of the X and Z distances), thickest
// right at its edge, thinning again further out so the distance
// stays clear
float seamFog(vec3 world) {
    vec2 d2 = abs(world.xz - uCamPos.xz);
    float d = max(d2.x, d2.y);
    return smoothstep(uSeamStart, uSeamEnd, d) * mix(1.0, 0.55, smoothstep(uSeamEnd + 200.0, uSeamEnd + 1400.0, d)) * uSeamHaze;
}

void main() {
    vec3 viewDir = normalize(vWorld - uCamPos);
    float s = pow(max(dot(viewDir, uSunDir), 0.0), 6.0);
    vec3 fogCol = mix(uFogHorizon, uFogSunGlow, s * 0.5 * (1.0 - uNight));
    float fog = max(1.0 - exp(-vDist * uFogDensity), seamFog(vWorld));
    // open ground under the sky (the colour has slope shading in it)
    // the same light as the voxels' top faces (shaders/chunk.frag): the
    // plain look keeps its old flat 0.82
    float sunVis = sunLight(vWorld);
    vec3 n = uSunShading != 0 ? terrainNormal(vWorld.xz) : vec3(0.0, 1.0, 0.0);
    float facing = (uSunShading != 0 || uTerrainShadows != 0 || uCloudShadows != 0)
                 ? 0.62 * mix(0.82, 1.0, sunVis) + 0.38 * max(dot(n, uLightDir), 0.0) * sunVis
                 : 0.82;
    vec3 lit = vColor * (uSkyLight * facing + uAmbient);
    if (vGlow > 0.5) { lit = vColor * 1.1; fog *= 0.3; }
    outColor = vec4(mix(lit, fogCol, fog), 1.0);
}
