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
in      vec2  vSpriteUV;
flat in float vSprite;
in      vec3  vSpriteNormal;
in      float vSpriteLow;
flat in float vPane;
uniform int   uPlantStyle;
// Window glass (blocks.toml render = "pane", filter): uTint is the
// colour the sky light came through glass in (world/LightBake.h tint;
// white where none). uPanePass 1: drawing the panes themselves, with
// dual-source blending (OpenGLRenderer::beginPanes): outColor is the
// pane's own light, outFilter what it lets through of what's behind
uniform sampler3D uTint;
uniform int       uPanePass;
// Water (Settings: Water): 0 plain, 1 moving. uFlow: which way it
// runs over each cell column (world/LightBake.h flow; rivers from
// the generator), uWaterTiles: water's top and side tiles
uniform int       uWaterStyle;
uniform sampler2D uFlow;
uniform vec4      uWaterTiles;
uniform float     uTime;

layout(location = 0, index = 0) out vec4 outColor;
layout(location = 0, index = 1) out vec4 outFilter;

uniform sampler2D uAtlas;
uniform vec2  uTileUV;          // size of one tile in atlas UVs
uniform vec3  uCamPos;
uniform float uVoxelsPerTile;   // voxels one tile stretches over
uniform usampler2D uCanopy;     // leaves over each cell column: count, lowest and highest leaf cell
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
// Haze at the voxel / far-terrain seam: measured along the square
// of voxel chunks (the larger of the X and Z distances), thickest
// right at its edge, thinning again further out so the distance
// stays clear
float seamFog(vec3 world) {
    vec2 d2 = abs(world.xz - uCamPos.xz);
    float d = max(d2.x, d2.y);
    return smoothstep(uSeamStart, uSeamEnd, d) * mix(1.0, 0.55, smoothstep(uSeamEnd + 200.0, uSeamEnd + 1400.0, d)) * uSeamHaze;
}
float fogAmount(float d) {
    float f = 1.0 - exp(-d * uFogDensity);
    return max(f, seamFog(vWorld));
}

// How bright block light is by its baked level (0..1). Settings >
// Graphics > Light fade: 0 classic (squared: bright close, gone well
// before the light's reach), 1 gentle (carries out to its full reach)
uniform int uLightCurve;
vec3 blockCurve(vec3 l) { return uLightCurve == 0 ? l * l * 1.5 : pow(l, vec3(1.1)) * 1.25; }

void main() {
    vec2 facePos = vFacePos;
    float waterGlint = 0.0;
    if (uWaterStyle == 1 && vSprite < 0.5 && (vTile == uWaterTiles.xy || vTile == uWaterTiles.zw)) {
        // moving water: rivers carry the surface downstream (faster
        // where they drop), falls at their steps run down, still
        // water barely drifts
        vec2 fl = texture(uFlow, (vLocal.xz * 0.5 + 1.0) / 66.0).rg;
        float run = length(fl);
        if (vNormal.y > 0.5) {
            facePos -= fl * uTime * 9.0;
            facePos += vec2(sin(uTime * 0.7 + vWorld.z * 0.21), cos(uTime * 0.6 + vWorld.x * 0.19)) * (0.35 - 0.25 * min(run, 1.0));
            // streaks along the current
            vec2 across = run > 0.01 ? vec2(-fl.y, fl.x) / run : vec2(0.0);
            float lines = sin(dot(vWorld.xz, across) * 1.7 + sin(dot(vWorld.xz, fl) * 0.3 - uTime * 3.0) * 1.5);
            waterGlint = run * smoothstep(0.8, 1.0, lines) * (0.5 + 0.5 * sin(dot(vWorld.xz, fl) * 0.11 - uTime * 1.3)) * 0.14;
        } else if (abs(vNormal.y) < 0.5 && run > 0.05) {
            facePos.y -= uTime * 14.0;                   // a fall: down the face
            waterGlint = 0.06;
        }
    }
    vec2 inTile = fract(facePos / uVoxelsPerTile);
    vec2 uv     = (vTile + inTile) * uTileUV;
    if (vSprite > 0.5) uv = (vTile + vSpriteUV) * uTileUV;      // sprites: the whole picture once

    vec4 color = texture(uAtlas, uv);

    // Cut-out pixels (gaps between leaves)
    if (color.a < 0.1) discard;
    outFilter = vec4(0.0);

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
    // Under leaves the sky comes through filtered: dim, and mostly
    // green (the bake lets it through and counts the leaves above)
    uvec3 cn = texelFetch(uCanopy, clamp(ivec2(floor(p.xz * 0.5)) + 1, ivec2(0), textureSize(uCanopy, 0) - 1), 0).rgb;
    vec3 skyTint = vec3(1.0);
    if (cn.r > 0u) {
        float cy = (vWorld.y + vNormal.y * 0.5) * 0.5;
        float top = float(cn.b) + 1.0, bottom = float(cn.g);
        float above = cy < bottom ? float(cn.r) : float(cn.r) * clamp((top - cy) / max(1.0, top - bottom), 0.0, 1.0);
        float shade = clamp(above / 5.0, 0.0, 1.0);
        skyTint = mix(vec3(1.0), vec3(0.30, 0.50, 0.24), shade);
    }
    skyTint *= texture(uTint, tc / 66.0).rgb;       // through coloured glass
    vec3 block = blockCurve(L.rgb);
    // sun / moon side brighter, the rest lit by the sky dome
    vec3 nrm = vNormal;
    if (vSprite > 0.5 && uPlantStyle == 1) nrm = normalize(vSpriteNormal);    // a rounded clump
    float facing = 0.62 + 0.38 * max(dot(nrm, uLightDir), 0.0);
    facing *= mix(1.0, vShade, 0.35);
    if (vSprite > 0.5 && uPlantStyle == 1) facing *= mix(0.72, 1.0, clamp(vSpriteLow * 1.4, 0.0, 1.0));   // darker in among the stems
    vec3 lit = uSkyLight * skyTint * sky * facing + block * mix(1.0, vShade, 0.5) + uAmbient;
    if (vGlow > 0.5) lit = vec3(1.15);           // glowing blocks show at full brightness

    vec3 rgb = color.rgb * lit;
    rgb += waterGlint * lit;                     // light on the moving water
    vec3 viewDir = normalize(vWorld - uCamPos);
    float fog = fogAmount(vDist);
    if (vGlow > 0.5) fog *= 0.3;                 // lights shine through the haze
    outColor = vec4(mix(rgb, fogColor(viewDir), fog), color.a);

    if (vPane > 0.5) {
        // Glass: the lead and frame (dark texels) are solid; the panes
        // mostly let through what's behind, filtered by their colour
        // (the texel's own hue), with a little of their own colour and
        // a sheen toward the sky
        float lum = max(color.r, max(color.g, color.b));
        float solid = color.a < 0.99 ? color.a : smoothstep(0.42, 0.28, lum);
        float own = mix(0.16, 1.0, solid);
        vec3 hue = color.rgb / max(lum, 0.05);
        vec3 pass = mix(vec3(1.0), hue, 0.65) * 0.86 * (1.0 - own);
        float sheen = pow(1.0 - abs(dot(normalize(vNormal), -viewDir)), 3.0) * 0.25 * (1.0 - solid);
        vec3 c = rgb * own + uSkyLight * sky * sheen;
        outColor = vec4(mix(c, fogColor(viewDir) * own, fog), 1.0);
        outFilter = vec4(mix(pass, vec3(1.0 - own), fog), 1.0);
        if (uPanePass == 0) discard;              // (never drawn with the solid world)
    }
}
