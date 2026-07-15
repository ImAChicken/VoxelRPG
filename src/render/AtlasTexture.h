#pragma once
#include <glad/glad.h>
#include <string>
#include <iostream>
#include "../vendor/stb_image.h"

class AtlasTexture {
public:
    GLuint id         = 0;
    int    width      = 0;
    int    height     = 0;
    int    tileSize   = 16;
    int    atlasCols  = 16;

    bool load(const std::string& path, int tileSize = 16, int cols = 16) {
        this->tileSize  = tileSize;
        this->atlasCols = cols;

        stbi_set_flip_vertically_on_load(false);

        int channels;
        unsigned char* data = stbi_load(
            path.c_str(), &width, &height, &channels, 4);

        if (!data) {
            std::cerr << "Failed to load atlas: " << path << "\n";
            std::cerr << "stb error: " << stbi_failure_reason() << "\n";
            return false;
        }

        glGenTextures(1, &id);
        glBindTexture(GL_TEXTURE_2D, id);

        // Nearest neighbor — crisp pixel art look
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA,
            width, height, 0,
            GL_RGBA, GL_UNSIGNED_BYTE, data);

        stbi_image_free(data);
        glBindTexture(GL_TEXTURE_2D, 0);

        std::cout << "Atlas loaded: " << path
                  << " (" << width << "x" << height << ")\n";
        return true;
    }

    // Get UV coordinates for a tile at (tx, ty) in the atlas grid
    // Returns bottom-left UV of the tile
    // face = 0 top, 1 bottom, 2-5 sides
    void getTileUV(int tx, int ty,
                   float& u0, float& v0,
                   float& u1, float& v1) const {
        float invW = 1.0f / (float)width;
        float invH = 1.0f / (float)height;

        u0 = (float)(tx * tileSize)           * invW;
        v0 = (float)(ty * tileSize)           * invH;
        u1 = (float)((tx + 1) * tileSize - 1) * invW;
        v1 = (float)((ty + 1) * tileSize - 1) * invH;
    }

    void bind(int unit = 0) const {
        glActiveTexture(GL_TEXTURE0 + unit);
        glBindTexture(GL_TEXTURE_2D, id);
    }

    void cleanup() {
        if (id) { glDeleteTextures(1, &id); id = 0; }
    }
};