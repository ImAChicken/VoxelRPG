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

out vec4 outColor;

uniform sampler2D uAtlas;
uniform vec2  uTileUV;          // size of one tile in atlas UVs
uniform float uVoxelsPerTile;   // voxels one tile stretches over

void main() {
    vec2 inTile = fract(vFacePos / uVoxelsPerTile);
    vec2 uv     = (vTile + inTile) * uTileUV;

    vec4 color = texture(uAtlas, uv);

    // Cut-out pixels (gaps between leaves)
    if (color.a < 0.1) discard;

    outColor = vec4(color.rgb * vShade, color.a);
}
