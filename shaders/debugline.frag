#version 460 core

// ── Debug line fragment shader ────────────────────────────
// uAlpha dims the pass drawn through walls.
in vec4 vColor;
uniform float uAlpha;
out vec4 FragColor;

void main() {
    FragColor = vec4(vColor.rgb, vColor.a * uAlpha);
}
