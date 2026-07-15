#pragma once
#include <glad/glad.h>
#include <vector>
#include <functional>
#include <glm/glm.hpp>
#include "chunk/Chunk.h"
#include "voxel/VoxelRegistry.h"
#include "render/AtlasTexture.h"
#include "render/ResourcePack.h"

// Vertex now has position, UV, and shade
struct Vertex {
    float x, y, z;
    float u, v;
    float shade;
};

// Face shade values
static const float FACE_SHADE[] = {
    1.0f,   // top
    0.6f,   // bottom
    0.8f,   // north
    0.8f,   // south
    0.7f,   // west
    0.7f,   // east
};

using NeighbourSampler = std::function<uint16_t(int, int, int)>;

class Mesher {
public:
    Mesher() = default;
    Mesher(const Mesher&)            = delete;
    Mesher& operator=(const Mesher&) = delete;

    Mesher(Mesher&& other) noexcept
        : vertices(std::move(other.vertices))
        , vao(other.vao), vbo(other.vbo)
        , vertexCount(other.vertexCount)
        , offset(other.offset)
    {
        other.vao = 0; other.vbo = 0; other.vertexCount = 0;
    }

    Mesher& operator=(Mesher&& other) noexcept {
        if (this != &other) {
            cleanup();
            vertices    = std::move(other.vertices);
            vao         = other.vao;
            vbo         = other.vbo;
            vertexCount = other.vertexCount;
            offset      = other.offset;
            other.vao = 0; other.vbo = 0; other.vertexCount = 0;
        }
        return *this;
    }

    int buildMesh(const Chunk&       chunk,
                  NeighbourSampler   sampleNeighbour,
                  glm::vec3          worldOffset,
                  const AtlasTexture& atlas) {
        vertices.clear();
        offset = worldOffset;

        for (int x = 0; x < VOXEL_DIM; x++)
        for (int y = 0; y < VOXEL_DIM; y++)
        for (int z = 0; z < VOXEL_DIM; z++) {
            uint16_t id = chunk.typeAt(x, y, z);
            if (id == BLOCK_AIR) continue;

            const VoxelTypeDef& def = gRegistry.get(id);

            // Get face UVs from resource pack
            FaceUV fuv = gResourcePacks.getFaceUV(def.name);

            if (sampleNeighbour(x, y+1, z) == BLOCK_AIR)
                addFace(x,y,z, 0, fuv.topX,  fuv.topY,  atlas);
            if (sampleNeighbour(x, y-1, z) == BLOCK_AIR)
                addFace(x,y,z, 1, fuv.botX,  fuv.botY,  atlas);
            if (sampleNeighbour(x, y, z+1) == BLOCK_AIR)
                addFace(x,y,z, 2, fuv.sideX, fuv.sideY, atlas);
            if (sampleNeighbour(x, y, z-1) == BLOCK_AIR)
                addFace(x,y,z, 3, fuv.sideX, fuv.sideY, atlas);
            if (sampleNeighbour(x-1, y, z) == BLOCK_AIR)
                addFace(x,y,z, 4, fuv.sideX, fuv.sideY, atlas);
            if (sampleNeighbour(x+1, y, z) == BLOCK_AIR)
                addFace(x,y,z, 5, fuv.sideX, fuv.sideY, atlas);
        }

        uploadToGPU();
        return (int)vertices.size();
    }

    void draw() const {
        if (vertexCount == 0) return;
        glBindVertexArray(vao);
        glDrawArrays(GL_TRIANGLES, 0, vertexCount);
        glBindVertexArray(0);
    }

    void cleanup() {
        if (vao) { glDeleteVertexArrays(1, &vao); vao = 0; }
        if (vbo) { glDeleteBuffers(1, &vbo);      vbo = 0; }
        vertexCount = 0;
    }

private:
    std::vector<Vertex> vertices;
    GLuint vao = 0, vbo = 0;
    int vertexCount = 0;
    glm::vec3 offset = {0,0,0};

    void addFace(int x, int y, int z,
                 int face,
                 int tx, int ty,
                 const AtlasTexture& atlas)
    {
        float shade = FACE_SHADE[face];
        float fx = (float)x + offset.x;
        float fy = (float)y + offset.y;
        float fz = (float)z + offset.z;

        float u0,v0,u1,v1;
        atlas.getTileUV(tx, ty, u0, v0, u1, v1);

        switch (face) {
            case 0: // top
                addQuad(fx,fy+1,fz,   u0,v0,
                        fx+1,fy+1,fz, u1,v0,
                        fx+1,fy+1,fz+1,u1,v1,
                        fx,fy+1,fz+1, u0,v1, shade); break;
            case 1: // bottom
                addQuad(fx,fy,fz+1,   u0,v1,
                        fx+1,fy,fz+1, u1,v1,
                        fx+1,fy,fz,   u1,v0,
                        fx,fy,fz,     u0,v0, shade); break;
            case 2: // north z+
                addQuad(fx+1,fy,fz+1, u0,v1,
                        fx,fy,fz+1,   u1,v1,
                        fx,fy+1,fz+1, u1,v0,
                        fx+1,fy+1,fz+1,u0,v0, shade); break;
            case 3: // south z-
                addQuad(fx,fy,fz,     u0,v1,
                        fx+1,fy,fz,   u1,v1,
                        fx+1,fy+1,fz, u1,v0,
                        fx,fy+1,fz,   u0,v0, shade); break;
            case 4: // west x-
                addQuad(fx,fy,fz+1,   u0,v1,
                        fx,fy,fz,     u1,v1,
                        fx,fy+1,fz,   u1,v0,
                        fx,fy+1,fz+1, u0,v0, shade); break;
            case 5: // east x+
                addQuad(fx+1,fy,fz,   u0,v1,
                        fx+1,fy,fz+1, u1,v1,
                        fx+1,fy+1,fz+1,u1,v0,
                        fx+1,fy+1,fz, u0,v0, shade); break;
        }
    }

    void addQuad(float x0,float y0,float z0, float u0,float v0,
                 float x1,float y1,float z1, float u1,float v1,
                 float x2,float y2,float z2, float u2,float v2,
                 float x3,float y3,float z3, float u3,float v3,
                 float shade)
    {
        vertices.push_back({x0,y0,z0,u0,v0,shade});
        vertices.push_back({x1,y1,z1,u1,v1,shade});
        vertices.push_back({x2,y2,z2,u2,v2,shade});
        vertices.push_back({x0,y0,z0,u0,v0,shade});
        vertices.push_back({x2,y2,z2,u2,v2,shade});
        vertices.push_back({x3,y3,z3,u3,v3,shade});
    }

    void uploadToGPU() {
        if (vao == 0) {
            glGenVertexArrays(1, &vao);
            glGenBuffers(1, &vbo);
        }
        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER,
            vertices.size() * sizeof(Vertex),
            vertices.data(), GL_STATIC_DRAW);

        // Position
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE,
            sizeof(Vertex), (void*)offsetof(Vertex, x));
        glEnableVertexAttribArray(0);
        // UV
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE,
            sizeof(Vertex), (void*)offsetof(Vertex, u));
        glEnableVertexAttribArray(1);
        // Shade
        glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE,
            sizeof(Vertex), (void*)offsetof(Vertex, shade));
        glEnableVertexAttribArray(2);

        glBindVertexArray(0);
        vertexCount = (int)vertices.size();
    }
};