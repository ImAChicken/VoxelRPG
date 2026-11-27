#version 460 core

// ── The sky world's far view (render/SkyFarView.h) ──────────
// Lit like the voxel chunks' faces by the sky of the moment (tops
// brightest, undersides darkest), hazed like the height-map far view
// (shaders/far.frag). Glowing cells keep their colour day and night.

in vec3 vColor;
in vec3 vNormal;
in vec3 vWorld;
in float vDist;
in float vGlow;

out vec4 outColor;

uniform vec3  uCamPos;
uniform vec3  uSkyLight, uAmbient, uFogHorizon, uFogSunGlow, uSunDir, uLightDir;
uniform float uFogDensity, uNight;

void main() {
    vec3 n = normalize(vNormal);
    vec3 viewDir = normalize(vWorld - uCamPos);
    float s = pow(max(dot(viewDir, uSunDir), 0.0), 6.0);
    vec3 fogCol = mix(uFogHorizon, uFogSunGlow, s * 0.5 * (1.0 - uNight));
    float fog = 1.0 - exp(-vDist * uFogDensity);
    // the voxels' face shading: top 1.0, sides 0.8 / 0.7, bottom 0.55,
    // blended by the normal (the smooth skin's lean between them)
    float side = mix(0.7, 0.8, abs(n.x) / max(abs(n.x) + abs(n.z), 1e-4));
    float face = n.y >= 0.0 ? mix(side, 1.0, n.y * n.y) : mix(side, 0.55, n.y * n.y);
    float sun = max(dot(n, uLightDir), 0.0);
    vec3 lit = vColor * (uSkyLight * (0.7 * face + 0.3 * sun) + uAmbient);
    if (vGlow > 0.5) { lit = vColor * 1.15; fog *= 0.3; }
    outColor = vec4(mix(lit, fogCol, fog), 1.0);
}
