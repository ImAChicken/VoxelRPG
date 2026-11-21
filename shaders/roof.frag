#version 460 core

// ── Roof fragment shader ──────────────────────────────────
// Courses of tiles, slates or thatch over the roof block's own
// texture (render/RoofRenderer.h):
//   - rows run along the eave, each a wedge: its lower lip leans
//     toward the light, its top sits in the shade of the row above
//   - tiles and slates: joints between them, every row shifted half
//     a piece, each piece a slightly different tone
//   - thatch: thicker rows, combed streaks down the slope, a ragged lip
//   - the texture under each row is shifted, so it doesn't repeat
// Trim (style 3: the fascia, barge boards, soffit) is the block's
// side texture, a little darker. Lighting and haze as props.

in      vec2  vUV;
flat in vec2  vTile;
flat in uint  vStyle;
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

float hash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }

vec3 fogColor(vec3 viewDir) {
    float s = pow(max(dot(viewDir, uSunDir), 0.0), 6.0);
    return mix(uFogHorizon, uFogSunGlow, s * 0.5 * (1.0 - uNight));
}

// How bright block light is by its baked level (0..1). Settings >
// Graphics > Light fade: 0 classic (squared: bright close, gone well
// before the light's reach), 1 gentle (carries out to its full reach)
uniform int uLightCurve;
#include "terrainlight.glsl"
vec3 blockCurve(vec3 l) { return uLightCurve == 0 ? l * l * 1.5 : pow(l, vec3(1.1)) * 1.25; }

void main() {
    vec3 n = normalize(vNormal);
    // down the slope, along the surface (for the rows' tilt)
    vec3 down = normalize(vec3(0.0, -1.0, 0.0) - n * dot(vec3(0.0, -1.0, 0.0), n));
    vec2 uv = vUV;
    float tone = 1.0;
    vec2 texShift = vec2(0.0);

    if (vStyle < 3u) {
        bool thatch = vStyle == 2u;
        float rowH = thatch ? 2.0 : (vStyle == 1u ? 1.25 : 1.5);   // voxels up the slope
        float pieceW = vStyle == 1u ? 2.0 : 1.5;
        float rv = uv.y / rowH;
        float row = floor(rv);
        float f = fract(rv);                     // 0 at a row's lower lip, 1 under the next row
        if (thatch) {
            // a ragged lip: the row's bottom edge wanders
            float rag = (hash(vec2(floor(uv.x * 3.0), row)) - 0.5) * 0.22;
            f = fract(rv + rag);
            row = floor(rv + rag);
            float streak = hash(vec2(floor(uv.x * 4.0), row * 1.7));
            tone *= 0.9 + 0.18 * streak;
            tone *= 0.92 + 0.08 * sin(uv.x * 9.0 + row * 2.3);
        } else {
            float shift = mod(row, 2.0) * 0.5 * pieceW;
            float px = (uv.x + shift) / pieceW;
            float piece = floor(px);
            float g = fract(px);
            tone *= 0.88 + 0.22 * hash(vec2(piece, row));
            float joint = smoothstep(0.0, 0.06, g) * smoothstep(1.0, 0.94, g);
            tone *= mix(0.62, 1.0, joint);
            texShift = vec2(hash(vec2(piece, row)) * 4.0, row * 1.37);
        }
        if (thatch) texShift = vec2(row * 2.1, row * 0.73);
        // the wedge: the lip catches the light, the top is shaded by the row above
        n = normalize(n + down * (0.55 - f) * (thatch ? 0.5 : 0.7));
        tone *= mix(0.72, 1.0, smoothstep(0.0, 0.35, 1.0 - f));      // under the next row's lip
        tone *= f < 0.08 ? 1.12 : 1.0;                               // the lip's edge
    } else {
        tone = 0.82;
    }

    vec2 inTile = fract((uv + texShift) / uVoxelsPerTile);
    vec4 color = texture(uAtlas, (vTile + inTile) * uTileUV);
    color.a = 1.0;

    float sky = vLight.a * vLight.a;
    vec3 skyTint = mix(vec3(1.0), vec3(0.30, 0.50, 0.24), clamp(vShade, 0.0, 1.0));
    vec3 block = blockCurve(vLight.rgb);
    float sunVis = sunLight(vWorld + n * 0.5);
    float facing = 0.62 * mix(0.82, 1.0, sunVis) + 0.38 * max(dot(n, uLightDir), 0.0) * sunVis;
    float up = 0.8 + 0.2 * clamp(n.y, -1.0, 1.0);
    vec3 lit = uSkyLight * skyTint * sky * facing * mix(1.0, up, 0.35) + block * mix(1.0, up, 0.5) + uAmbient;

    vec3 rgb = color.rgb * tone * lit;
    vec3 viewDir = normalize(vWorld - uCamPos);
    float fog = 1.0 - exp(-vDist * uFogDensity);
    outColor = vec4(mix(rgb, fogColor(viewDir), fog), 1.0);
}
