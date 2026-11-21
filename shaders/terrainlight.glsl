// ── Sun and shade from the shape of the land ─────────────
// (src/render/TerrainLight.h; pulled into chunk, prop, roof and far
// shaders by Shader's #include). Each part is a setting; off (or no
// map yet) leaves the plain look.
uniform sampler2D uTerrainH;     // ground height per cell (voxels)
uniform sampler2D uTerrainS;     // shadow height per cell for the light now: lower is behind a hill
uniform vec4  uTerrainMap;       // 1 / world width, 1 / world depth, cell size, 1 when the map is ready
uniform int   uSunShading, uTerrainShadows, uCloudShadows;
uniform vec3  uClouds;           // wind direction (xz), seconds

vec2 tlUV(vec2 xz) { return xz * uTerrainMap.xy; }
float terrainHeight(vec2 xz) { return texture(uTerrainH, tlUV(xz)).r; }

// The ground's smooth normal (the height map's slope across a cell each way)
vec3 terrainNormal(vec2 xz) {
    float c = uTerrainMap.z;
    float l = terrainHeight(xz - vec2(c, 0.0)), r = terrainHeight(xz + vec2(c, 0.0));
    float d = terrainHeight(xz - vec2(0.0, c)), u = terrainHeight(xz + vec2(0.0, c));
    return normalize(vec3(l - r, 2.0 * c, d - u));
}
// How near p is to the ground the map knows (1 on it, 0 a few voxels off):
// only there does the smooth normal stand in for a face's own
float nearTerrain(vec3 p) {
    if (uSunShading == 0 || uTerrainMap.w < 0.5) return 0.0;
    return 1.0 - smoothstep(uTerrainMap.z * 0.6, uTerrainMap.z * 1.6, abs(p.y - terrainHeight(p.xz)));
}

// Value noise for the clouds
float tlHash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
float tlNoise(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(tlHash(i), tlHash(i + vec2(1, 0)), f.x), mix(tlHash(i + vec2(0, 1)), tlHash(i + vec2(1, 1)), f.x), f.y);
}
float cloudLight(vec2 xz) {
    if (uCloudShadows == 0) return 1.0;
    vec2 q = xz / 900.0 - uClouds.xy * uClouds.z * 0.012;
    float n = tlNoise(q) * 0.55 + tlNoise(q * 2.1 + 7.3) * 0.3 + tlNoise(q * 4.3 + 2.1) * 0.15;
    return 1.0 - 0.6 * smoothstep(0.52, 0.68, n);
}
// 0..1: how much of the sun (or moon) gets to p: past the hills, through the clouds
float sunLight(vec3 p) {
    float v = 1.0;
    if (uTerrainShadows != 0 && uTerrainMap.w > 0.5) {
        float s = texture(uTerrainS, tlUV(p.xz)).r;
        float bias = uTerrainMap.z * 0.75;        // (the map is coarse: the ground itself stays lit)
        v = smoothstep(s - bias - 8.0, s - bias, p.y);
    }
    return v * cloudLight(p.xz);
}
