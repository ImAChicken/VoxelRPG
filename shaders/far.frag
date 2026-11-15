#version 460 core

// ── Distant terrain fragment shader ───────────────────────

in vec3 vColor;
in float vDist;

out vec4 outColor;

uniform vec3  uFogColor;
uniform float uFogDensity;

void main() {
    float fog = 1.0 - exp(-vDist * uFogDensity);
    outColor = vec4(mix(vColor, uFogColor, fog), 1.0);
}
