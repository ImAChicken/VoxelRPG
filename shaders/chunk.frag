#version 460 core

// ── Chunk fragment shader ─────────────────────────────────
// Each voxel face shows a 4 × 4 pixel piece of its 16 × 16
// atlas tile, so the full tile spans 4 voxels and then
// repeats. Greedy quads can be 128 voxels wide, so the
// repeat has to happen here, per pixel:
//
//   1. fract() wraps the position into 0..1 within one tile
//   2. add the tile's column/row in the atlas grid
//   3. scale from "tiles" to real atlas UVs
//
// The lookup can never leave its own tile, so neighbouring
// tiles never bleed in.

in      vec2  vFacePos;
flat in vec2  vTile;
flat in float vShade;
in      float vDist;
in      vec3  vLocal;
in      vec3  vWorld;
flat in vec3  vNormal;
flat in float vGlow;

out vec4 outColor;

uniform sampler2D uAtlas;
uniform vec2  uTileUV;          // size of one tile in atlas UVs
uniform vec3  uCamPos;
uniform float uVoxelsPerTile;   // voxels one tile stretches over
uniform sampler3D uLight;       // the baked light layer for this section (66³ cells, RGB = block light, A = sky)

// ── Lighting and haze, shared with the distant terrain ──
uniform vec3  uSkyLight;        // full sky light now (sun by day, blue moon by night)
uniform vec3  uLightDir;        // toward the sun or moon
uniform vec3  uAmbient;         // the floor, deep inside rock
uniform vec3  uFogHorizon, uFogSunGlow, uSunDir;
uniform float uFogDensity, uSeamStart, uSeamEnd, uSeamHaze, uNight;

vec3 fogColor(vec3 viewDir) {
    float s = pow(max(dot(viewDir, uSunDir), 0.0), 6.0);
    return mix(uFogHorizon, uFogSunGlow, s * 0.5 * (1.0 - uNight));
}
float fogAmount(float d) {
    float f = 1.0 - exp(-d * uFogDensity);
    return max(f, smoothstep(uSeamStart, uSeamEnd, d) * uSeamHaze);
}

void main() {
    vec2 inTile = fract(vFacePos / uVoxelsPerTile);
    vec2 uv     = (vTile + inTile) * uTileUV;

    vec4 color = texture(uAtlas, uv);

    // Cut-out pixels (gaps between leaves)
    if (color.a < 0.1) discard;

    // light half a voxel out in front of the face, so a face takes
    // the light of the air it faces
    // Across the face the light blends smoothly from cell to cell;
    // along the normal it is pinned to the centre of the cell holding
    // that air voxel (and the next cell out, whichever is brighter), so
    // a face never blends in the solid cell behind it — otherwise
    // ground at odd and even heights would shade differently
    vec3 p  = vLocal + vNormal * 0.5;
    vec3 an = abs(vNormal);
    vec3 tc = mix(p * 0.5 + 1.0, floor(p * 0.5) + 1.5, an);
    vec4 L  = max(texture(uLight, tc / 66.0), texture(uLight, (tc + vNormal) / 66.0));
    float sky = L.a * L.a;                       // a gentler fall-off into shade
    vec3 block = L.rgb * L.rgb * 1.5;
    // sun / moon side brighter, the rest lit by the sky dome
    float facing = 0.62 + 0.38 * max(dot(vNormal, uLightDir), 0.0);
    facing *= mix(1.0, vShade, 0.35);
    vec3 lit = uSkyLight * sky * facing + block * mix(1.0, vShade, 0.5) + uAmbient;
    if (vGlow > 0.5) lit = vec3(1.15);           // glowing blocks show at full brightness

    vec3 rgb = color.rgb * lit;
    vec3 viewDir = normalize(vWorld - uCamPos);
    float fog = fogAmount(vDist);
    if (vGlow > 0.5) fog *= 0.3;                 // lights shine through the haze
    outColor = vec4(mix(rgb, fogColor(viewDir), fog), color.a);
}
