#version 460 core

// ── Prop fragment shader ──────────────────────────────────
// The chunk shader's look (atlas tiles, baked light, canopy
// tint, sun-facing shade, haze), with the light handed in per
// vertex instead of read from a section's light texture.

in      vec2  vFacePos;
flat in vec2  vTile;
flat in float vGlow;
in      vec3  vWorld;
in      vec3  vNormal;
in      float vDist;
in      vec4  vLight;
in      float vShade;

out vec4 outColor;

uniform sampler2D uAtlas;
uniform vec2  uTileUV;
uniform vec3  uCamPos;
uniform float uVoxelsPerTile;
uniform vec3  uSkyLight, uLightDir, uAmbient;
uniform vec3  uFogHorizon, uFogSunGlow, uSunDir;
uniform float uFogDensity, uNight;
uniform float uAlpha;       // < 1: a ghost (the hammer's preview of a piece)

vec3 fogColor(vec3 viewDir) {
    float s = pow(max(dot(viewDir, uSunDir), 0.0), 6.0);
    return mix(uFogHorizon, uFogSunGlow, s * 0.5 * (1.0 - uNight));
}

void main() {
    vec2 inTile = fract(vFacePos / uVoxelsPerTile);
    vec4 color = texture(uAtlas, (vTile + inTile) * uTileUV);
    if (color.a < 0.1) discard;

    vec3 n = normalize(vNormal);
    float faceShade = 0.8 + 0.2 * n.y;               // top 1.0, sides 0.8, bottom 0.6, as chunks
    float sky = vLight.a * vLight.a;
    vec3 skyTint = mix(vec3(1.0), vec3(0.30, 0.50, 0.24), clamp(vShade, 0.0, 1.0));
    vec3 block = vLight.rgb * vLight.rgb * 1.5;
    float facing = 0.62 + 0.38 * max(dot(n, uLightDir), 0.0);
    facing *= mix(1.0, faceShade, 0.35);
    vec3 lit = uSkyLight * skyTint * sky * facing + block * mix(1.0, faceShade, 0.5) + uAmbient;
    if (vGlow > 0.5) lit = vec3(1.15);

    vec3 rgb = color.rgb * lit;
    vec3 viewDir = normalize(vWorld - uCamPos);
    float fog = 1.0 - exp(-vDist * uFogDensity);
    outColor = vec4(mix(rgb, fogColor(viewDir), fog), color.a * uAlpha);
}
