#version 460 core

in vec2  fragUV;
in float fragShade;

out vec4 outColor;

uniform sampler2D uAtlas;
uniform int       uUseTexture;

void main() {
    vec4 color;

    if (uUseTexture == 1) {
        color = texture(uAtlas, fragUV);
        // Discard fully transparent pixels (leaves, glass edges)
        if (color.a < 0.1) discard;
    } else {
        // Fallback flat color from UV as color
        color = vec4(fragUV.x, fragUV.y, 0.5, 1.0);
    }

    outColor = vec4(color.rgb * fragShade, color.a);
}