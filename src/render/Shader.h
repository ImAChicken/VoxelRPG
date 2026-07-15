#pragma once
#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <string>
#include <fstream>
#include <sstream>
#include <iostream>

class Shader {
public:
    GLuint id = 0;

    bool load(const char* vertPath, const char* fragPath) {
        std::string vertSrc = readFile(vertPath);
        std::string fragSrc = readFile(fragPath);

        if (vertSrc.empty() || fragSrc.empty()) {
            std::cerr << "Failed to read shader files\n";
            return false;
        }

        const char* vSrc = vertSrc.c_str();
        const char* fSrc = fragSrc.c_str();

        GLuint vert = glCreateShader(GL_VERTEX_SHADER);
        glShaderSource(vert, 1, &vSrc, nullptr);
        glCompileShader(vert);
        if (!checkCompile(vert, "VERTEX")) return false;

        GLuint frag = glCreateShader(GL_FRAGMENT_SHADER);
        glShaderSource(frag, 1, &fSrc, nullptr);
        glCompileShader(frag);
        if (!checkCompile(frag, "FRAGMENT")) return false;

        id = glCreateProgram();
        glAttachShader(id, vert);
        glAttachShader(id, frag);
        glLinkProgram(id);
        if (!checkLink(id)) return false;

        glDeleteShader(vert);
        glDeleteShader(frag);

        std::cout << "Shaders loaded OK\n";
        return true;
    }

    void use() const { glUseProgram(id); }

    void setMat4(const char* name, const glm::mat4& m) const {
        glUniformMatrix4fv(
            glGetUniformLocation(id, name),
            1, GL_FALSE, glm::value_ptr(m));
    }

    void setInt(const char* name, int value) const {
        glUniform1i(glGetUniformLocation(id, name), value);
    }

    void setFloat(const char* name, float value) const {
        glUniform1f(glGetUniformLocation(id, name), value);
    }

private:
    std::string readFile(const char* path) {
        std::ifstream f(path);
        if (!f.is_open()) {
            std::cerr << "Cannot open: " << path << "\n";
            return "";
        }
        std::stringstream ss;
        ss << f.rdbuf();
        return ss.str();
    }

    bool checkCompile(GLuint shader, const char* type) {
        GLint ok;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            char log[512];
            glGetShaderInfoLog(shader, 512, nullptr, log);
            std::cerr << type << " shader error:\n" << log << "\n";
            return false;
        }
        return true;
    }

    bool checkLink(GLuint program) {
        GLint ok;
        glGetProgramiv(program, GL_LINK_STATUS, &ok);
        if (!ok) {
            char log[512];
            glGetProgramInfoLog(program, 512, nullptr, log);
            std::cerr << "Shader link error:\n" << log << "\n";
            return false;
        }
        return true;
    }
};