#version 460 core

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 aUV;
layout(location = 2) in float aShade;

out vec2 fragUV;
out float fragShade;

uniform mat4 uView;
uniform mat4 uProjection;

void main() {
    fragUV    = aUV;
    fragShade = aShade;
    gl_Position = uProjection * uView * vec4(aPos, 1.0);
}