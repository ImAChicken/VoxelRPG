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

void main() {
    vec3 viewDir = normalize(vWorld - uCamPos);
    float s = pow(max(dot(viewDir, uSunDir), 0.0), 6.0);
    vec3 fogCol = mix(uFogHorizon, uFogSunGlow, s * 0.5 * (1.0 - uNight));
    float fog = max(1.0 - exp(-vDist * uFogDensity), smoothstep(uSeamStart, uSeamEnd, vDist) * uSeamHaze);
    // open ground under the sky (the colour has slope shading in it)
    vec3 lit = vColor * (uSkyLight * 0.82 + uAmbient);
    if (vGlow > 0.5) { lit = vColor * 1.1; fog *= 0.3; }
    outColor = vec4(mix(lit, fogCol, fog), 1.0);
}
