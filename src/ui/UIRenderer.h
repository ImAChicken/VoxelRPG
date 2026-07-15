#pragma once
#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <vector>
#include <string>
#include <iostream>
#include "../vendor/stb_image.h"
#include "../render/ResourcePack.h"

class UIRenderer {
public:
    bool init(int screenW, int screenH) {
        width  = screenW;
        height = screenH;

        const char* vertSrc =
            "#version 460 core\n"
            "layout(location=0) in vec2 aPos;\n"
            "layout(location=1) in vec2 aUV;\n"
            "layout(location=2) in vec4 aColor;\n"
            "out vec2 fragUV;\n"
            "out vec4 fragColor;\n"
            "uniform mat4 uProj;\n"
            "void main() {\n"
            "    fragUV    = aUV;\n"
            "    fragColor = aColor;\n"
            "    gl_Position = uProj * vec4(aPos, 0.0, 1.0);\n"
            "}\n";

        const char* fragSrc =
            "#version 460 core\n"
            "in vec2  fragUV;\n"
            "in vec4  fragColor;\n"
            "out vec4 outColor;\n"
            "uniform sampler2D uTex;\n"
            "uniform int uUseTexture;\n"
            "void main() {\n"
            "    if (uUseTexture == 1) {\n"
            "        vec4 t = texture(uTex, fragUV);\n"
            "        if (t.a < 0.05) discard;\n"
            "        outColor = vec4(fragColor.rgb,"
            "                        fragColor.a * t.a);\n"
            "    } else {\n"
            "        outColor = fragColor;\n"
            "    }\n"
            "}\n";

        GLuint vert = glCreateShader(GL_VERTEX_SHADER);
        glShaderSource(vert, 1, &vertSrc, nullptr);
        glCompileShader(vert);
        if (!checkShader(vert, "UI VERT")) return false;

        GLuint frag = glCreateShader(GL_FRAGMENT_SHADER);
        glShaderSource(frag, 1, &fragSrc, nullptr);
        glCompileShader(frag);
        if (!checkShader(frag, "UI FRAG")) return false;

        program = glCreateProgram();
        glAttachShader(program, vert);
        glAttachShader(program, frag);
        glLinkProgram(program);
        glDeleteShader(vert);
        glDeleteShader(frag);

        glGenVertexArrays(1, &vao);
        glGenBuffers(1, &vbo);
        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE,
            sizeof(UIVertex), (void*)0);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE,
            sizeof(UIVertex), (void*)(2*sizeof(float)));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE,
            sizeof(UIVertex), (void*)(4*sizeof(float)));
        glEnableVertexAttribArray(2);
        glBindVertexArray(0);

        updateProjection();
        std::cout << "UIRenderer initialized\n";
        return true;
    }

    void resize(int w, int h) {
        width = w; height = h;
        updateProjection();
    }

    bool loadFont() {
        auto path = gResourcePacks.getFontPath();
        stbi_set_flip_vertically_on_load(false);
        int w, h, ch;
        unsigned char* data = stbi_load(
            path.string().c_str(), &w, &h, &ch, 4);
        if (!data) {
            std::cerr << "Font load failed: "
                      << path << "\n";
            return false;
        }
        glGenTextures(1, &fontTex);
        glBindTexture(GL_TEXTURE_2D, fontTex);
        glTexParameteri(GL_TEXTURE_2D,
            GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D,
            GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA,
            w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
        stbi_image_free(data);
        fontW = w; fontH = h;

        const auto& fc = gResourcePacks.getFontConfig();
        fontCharW = fc.charW;
        fontCharH = fc.charH;
        fontCols  = fc.cols;
        fontFirst = fc.firstChar;
        std::cout << "Font loaded: " << path << "\n";
        return true;
    }

    void begin() {
        glDisable(GL_DEPTH_TEST);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glUseProgram(program);
        glUniformMatrix4fv(
            glGetUniformLocation(program, "uProj"),
            1, GL_FALSE, glm::value_ptr(proj));
        verts.clear();
        useTexture = false;
    }

    void end() {
        flush();
        glEnable(GL_DEPTH_TEST);
    }

    void drawRect(float x, float y, float w, float h,
                  const float* color) {
        float r = color[0]/255.f, g = color[1]/255.f,
              b = color[2]/255.f, a = color[3]/255.f;
        pushQuad(x, y, w, h, 0,0,1,1, r,g,b,a, false);
    }

    void drawBorder(float x, float y, float w, float h,
                    const float* color,
                    float thickness = 1.0f) {
        drawRect(x, y, w, thickness, color);
        drawRect(x, y+h-thickness, w, thickness, color);
        drawRect(x, y, thickness, h, color);
        drawRect(x+w-thickness, y, thickness, h, color);
    }

    void drawText(const std::string& text,
                  float x, float y,
                  const float* color,
                  int scale = 2) {
        if (fontTex == 0) return;
        float r = color[0]/255.f, g = color[1]/255.f,
              b = color[2]/255.f, a = color[3]/255.f;

        float cx2 = x;
        float fw = (float)fontCharW / (float)fontW;
        float fh = (float)fontCharH / (float)fontH;

        flushColored();
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, fontTex);
        glUniform1i(
            glGetUniformLocation(program,"uTex"), 0);
        glUniform1i(
            glGetUniformLocation(program,"uUseTexture"), 1);
        useTexture = true;

        for (char c : text) {
            int idx = (int)c - fontFirst;
            if (idx < 0) {
                cx2 += fontCharW * scale;
                continue;
            }
            int col = idx % fontCols;
            int row = idx / fontCols;
            float u0 = col * fw;
            float v0 = row * fh;
            float u1 = u0 + fw;
            float v1 = v0 + fh;
            pushQuad(cx2, y,
                     (float)(fontCharW * scale),
                     (float)(fontCharH * scale),
                     u0, v0, u1, v1,
                     r, g, b, a, true);
            cx2 += fontCharW * scale;
        }
        flush();
        glUniform1i(
            glGetUniformLocation(program,"uUseTexture"), 0);
        useTexture = false;
    }

    int textWidth(const std::string& text,
                  int scale = 2) const {
        return (int)text.size() * fontCharW * scale;
    }

    int textHeight(int scale = 2) const {
        return fontCharH * scale;
    }

    void drawTextCentered(const std::string& text,
                          float rx, float ry,
                          float rw, float rh,
                          const float* color,
                          int scale = 2) {
        float tw = (float)textWidth(text, scale);
        float th = (float)textHeight(scale);
        float tx = rx + (rw - tw) * 0.5f;
        float ty = ry + (rh - th) * 0.5f;
        drawText(text, tx, ty, color, scale);
    }

    void cleanup() {
        if (vao)    { glDeleteVertexArrays(1,&vao); vao=0; }
        if (vbo)    { glDeleteBuffers(1,&vbo);      vbo=0; }
        if (program){ glDeleteProgram(program); program=0; }
        if (fontTex){ glDeleteTextures(1,&fontTex); fontTex=0;}
    }

    int getWidth()  const { return width;  }
    int getHeight() const { return height; }

private:
    struct UIVertex {
        float x, y, u, v, r, g, b, a;
    };

    GLuint vao = 0, vbo = 0, program = 0;
    GLuint fontTex  = 0;
    int    width    = 1280, height = 720;
    int    fontW    = 128,  fontH  = 64;
    int    fontCharW= 8,    fontCharH = 8;
    int    fontCols = 16,   fontFirst = 32;
    glm::mat4 proj;
    std::vector<UIVertex> verts;
    bool useTexture = false;

    void updateProjection() {
        proj = glm::ortho(
            0.f, (float)width,
            (float)height, 0.f,
            -1.f, 1.f);
    }

    void pushQuad(float x, float y,
                  float w, float h,
                  float u0, float v0,
                  float u1, float v1,
                  float r,  float g,
                  float b,  float a,
                  bool tex) {
        if (tex != useTexture) { flush(); useTexture = tex; }
        verts.push_back({x,   y,   u0,v0, r,g,b,a});
        verts.push_back({x+w, y,   u1,v0, r,g,b,a});
        verts.push_back({x+w, y+h, u1,v1, r,g,b,a});
        verts.push_back({x,   y,   u0,v0, r,g,b,a});
        verts.push_back({x+w, y+h, u1,v1, r,g,b,a});
        verts.push_back({x,   y+h, u0,v1, r,g,b,a});
    }

    void flushColored() {
        if (!verts.empty() && !useTexture) flush();
    }

    void flush() {
        if (verts.empty()) return;
        glUniform1i(
            glGetUniformLocation(program, "uUseTexture"),
            useTexture ? 1 : 0);
        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER,
            verts.size() * sizeof(UIVertex),
            verts.data(), GL_DYNAMIC_DRAW);
        glDrawArrays(GL_TRIANGLES, 0,
            (GLsizei)verts.size());
        glBindVertexArray(0);
        verts.clear();
    }

    bool checkShader(GLuint s, const char* name) {
        GLint ok;
        glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            char log[512];
            glGetShaderInfoLog(s, 512, nullptr, log);
            std::cerr << name << " error: " << log << "\n";
            return false;
        }
        return true;
    }
};
