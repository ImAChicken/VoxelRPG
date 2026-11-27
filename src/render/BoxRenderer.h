#pragma once
#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <cmath>
#include <iostream>

// ══════════════════════════════════════════════════════════
// BoxRenderer — plain coloured boxes in the world
// ══════════════════════════════════════════════════════════
// A stand-in for things that have no model yet. Right now
// that is the player's body in third person; the real
// player model replaces it later. Also handy for debug
// markers.
//
// One unit cube lives on the GPU; each box is that cube
// stretched, turned and moved by a matrix.
class BoxRenderer {
public:
    bool init() {
        const char* vertSrc =
            "#version 460 core\n"
            "layout(location=0) in vec3 aPos;\n"
            "layout(location=1) in vec3 aNormal;\n"
            "uniform mat4 uViewProj;\n"
            "uniform mat4 uModel;\n"
            "out vec3 vNormal;\n"
            "void main() {\n"
            "    vNormal = mat3(uModel) * aNormal;\n"
            "    gl_Position = uViewProj * uModel * vec4(aPos, 1.0);\n"
            "}\n";

        // Simple fixed light from above so the faces of the
        // box can be told apart
        const char* fragSrc =
            "#version 460 core\n"
            "in vec3 vNormal;\n"
            "uniform vec3 uColor;\n"
            "out vec4 outColor;\n"
            "void main() {\n"
            "    vec3 n = normalize(vNormal);\n"
            "    float light = 0.65 + 0.35 *\n"
            "        dot(n, normalize(vec3(0.3, 1.0, 0.5)));\n"
            "    outColor = vec4(uColor * light, 1.0);\n"
            "}\n";

        GLuint vert = compile(GL_VERTEX_SHADER,   vertSrc);
        GLuint frag = compile(GL_FRAGMENT_SHADER, fragSrc);
        if (!vert || !frag) return false;

        program = glCreateProgram();
        glAttachShader(program, vert);
        glAttachShader(program, frag);
        glLinkProgram(program);
        glDeleteShader(vert);
        glDeleteShader(frag);

        GLint ok = 0;
        glGetProgramiv(program, GL_LINK_STATUS, &ok);
        if (!ok) {
            std::cerr << "BoxRenderer: shader link failed\n";
            return false;
        }

        locViewProj = glGetUniformLocation(program, "uViewProj");
        locModel    = glGetUniformLocation(program, "uModel");
        locColor    = glGetUniformLocation(program, "uColor");

        // ── Unit cube ─────────────────────────────────────
        // x and z run -0.5..+0.5, y runs 0..1, so a box is
        // positioned by the centre of its base — the same
        // way a body is positioned by its feet.
        // 6 faces × 2 triangles × 3 corners, each corner
        // = position (3 floats) + face normal (3 floats).
        static const float faces[6][4][3] = {
            {{ .5f,0,-.5f},{ .5f,1,-.5f},{ .5f,1, .5f},{ .5f,0, .5f}}, // +X
            {{-.5f,0, .5f},{-.5f,1, .5f},{-.5f,1,-.5f},{-.5f,0,-.5f}}, // -X
            {{-.5f,1,-.5f},{-.5f,1, .5f},{ .5f,1, .5f},{ .5f,1,-.5f}}, // +Y
            {{-.5f,0, .5f},{-.5f,0,-.5f},{ .5f,0,-.5f},{ .5f,0, .5f}}, // -Y
            {{ .5f,0, .5f},{ .5f,1, .5f},{-.5f,1, .5f},{-.5f,0, .5f}}, // +Z
            {{-.5f,0,-.5f},{-.5f,1,-.5f},{ .5f,1,-.5f},{ .5f,0,-.5f}}, // -Z
        };
        static const float normals[6][3] = {
            {1,0,0}, {-1,0,0}, {0,1,0}, {0,-1,0}, {0,0,1}, {0,0,-1},
        };
        static const int corner[6] = {0, 1, 2, 0, 2, 3};

        float data[6 * 6 * 6];
        int n = 0;
        for (int f = 0; f < 6; f++)
        for (int c = 0; c < 6; c++) {
            for (int k = 0; k < 3; k++)
                data[n++] = faces[f][corner[c]][k];
            for (int k = 0; k < 3; k++)
                data[n++] = normals[f][k];
        }

        glGenVertexArrays(1, &vao);
        glGenBuffers(1, &vbo);
        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(data), data,
                     GL_STATIC_DRAW);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE,
            6 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE,
            6 * sizeof(float), (void*)(3 * sizeof(float)));
        glEnableVertexAttribArray(1);
        glBindVertexArray(0);
        return true;
    }

    // Draw one box.
    //   base    centre of the bottom face, world voxels
    //   size    width (x), height (y), depth (z)
    //   yawDeg  turn about the vertical axis; 0 = the box's
    //           +X side faces world +X
    //   color   red, green, blue, each 0..1
    void draw(const glm::mat4& viewProj, glm::vec3 base,
              glm::vec3 size, float yawDeg, glm::vec3 color, bool keepCull = false) {
        if (!program) return;

        glm::mat4 model(1.0f);
        model = glm::translate(model, base);
        // Camera yaw turns from +X towards +Z; a rotation
        // about +Y turns the other way, hence the minus
        model = glm::rotate(model, glm::radians(-yawDeg),
                            glm::vec3(0.0f, 1.0f, 0.0f));
        model = glm::scale(model, size);

        glEnable(GL_DEPTH_TEST);
        if (!keepCull) glDisable(GL_CULL_FACE);
        glUseProgram(program);
        glUniformMatrix4fv(locViewProj, 1, GL_FALSE,
                           glm::value_ptr(viewProj));
        glUniformMatrix4fv(locModel, 1, GL_FALSE,
                           glm::value_ptr(model));
        glUniform3f(locColor, color.x, color.y, color.z);
        glBindVertexArray(vao);
        glDrawArrays(GL_TRIANGLES, 0, 36);
        glBindVertexArray(0);
    }

    // A thin bar from a to b (a rope, a beam), 'thick' voxels across
    void beam(const glm::mat4& viewProj, glm::vec3 a, glm::vec3 b, float thick, glm::vec3 color) {
        if (!program) return;
        glm::vec3 d = b - a;
        float len = glm::length(d);
        if (len < 1e-4f) return;
        glm::vec3 u = d / len;
        glm::vec3 side = glm::cross(u, std::fabs(u.y) < 0.95f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0));
        side = glm::normalize(side) * thick;
        glm::vec3 up = glm::normalize(glm::cross(side, u)) * thick;
        glm::mat4 model(1.0f);
        model[0] = glm::vec4(d, 0.0f);
        model[1] = glm::vec4(up, 0.0f);
        model[2] = glm::vec4(side, 0.0f);
        model[3] = glm::vec4((a + b) * 0.5f - up * 0.5f, 1.0f);   // (the cube: x and z -0.5..0.5, y 0..1)
        glEnable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        glUseProgram(program);
        glUniformMatrix4fv(locViewProj, 1, GL_FALSE, glm::value_ptr(viewProj));
        glUniformMatrix4fv(locModel, 1, GL_FALSE, glm::value_ptr(model));
        glUniform3f(locColor, color.x, color.y, color.z);
        glBindVertexArray(vao);
        glDrawArrays(GL_TRIANGLES, 0, 36);
        glBindVertexArray(0);
    }

    // The toon outline of a box (the characters' edge lines, for
    // bodies drawn as boxes): its back faces only, grown by
    // 'thick' voxels all round, in the line colour. Drawn before or
    // after the box itself; the box's own faces hide the inside.
    void drawOutline(const glm::mat4& viewProj, glm::vec3 base, glm::vec3 size, float yawDeg,
                     glm::vec3 color, float thick) {
        if (!program) return;
        glEnable(GL_CULL_FACE);
        glCullFace(GL_FRONT);                      // the faces turned away: a shell behind the box
        draw(viewProj, base - glm::vec3(0.0f, thick, 0.0f), size + glm::vec3(thick * 2.0f), yawDeg, color, true);
        glCullFace(GL_BACK);
        glDisable(GL_CULL_FACE);
    }

    void cleanup() {
        if (vbo)     glDeleteBuffers(1, &vbo);
        if (vao)     glDeleteVertexArrays(1, &vao);
        if (program) glDeleteProgram(program);
        vbo = vao = program = 0;
    }

private:
    GLuint program = 0, vao = 0, vbo = 0;
    GLint  locViewProj = -1, locModel = -1, locColor = -1;

    static GLuint compile(GLenum type, const char* src) {
        GLuint s = glCreateShader(type);
        glShaderSource(s, 1, &src, nullptr);
        glCompileShader(s);
        GLint ok = 0;
        glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            char log[512];
            glGetShaderInfoLog(s, sizeof(log), nullptr, log);
            std::cerr << "BoxRenderer: shader error: "
                      << log << "\n";
            glDeleteShader(s);
            return 0;
        }
        return s;
    }
};
