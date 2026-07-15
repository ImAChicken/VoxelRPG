#pragma once
#include <glad/glad.h>
#include <vector>
#include <glm/glm.hpp>
#include "chunk/Chunk.h"
#include <functional>

struct Vertex {
    float x, y, z;
    float r, g, b;
};

static const float BLOCK_COLORS[][3] = {
    {0.0f,  0.0f,  0.0f },  // 0 air
    {0.5f,  0.5f,  0.5f },  // 1 stone
    {0.6f,  0.4f,  0.2f },  // 2 dirt
    {0.3f,  0.7f,  0.2f },  // 3 grass
    {0.4f,  0.25f, 0.1f },  // 4 log
    {0.8f,  0.6f,  0.3f },  // 5 planks
};

static const float FACE_SHADE[] = {
    1.0f,  // top
    0.6f,  // bottom
    0.8f,  // north
    0.8f,  // south
    0.7f,  // west
    0.7f,  // east
};

// Neighbour sampler — function that returns block type at any local coord
// including coords outside this chunk (handled by caller)
using NeighbourSampler = std::function<uint16_t(int, int, int)>;

class Mesher {
public:
    // Build mesh with neighbour awareness for seam stitching
    int buildMesh(const Chunk& chunk,
                  NeighbourSampler sampleNeighbour,
                  glm::vec3 worldOffset) {
        vertices.clear();
        offset = worldOffset;

        for (int x = 0; x < VOXEL_DIM; x++)
        for (int y = 0; y < VOXEL_DIM; y++)
        for (int z = 0; z < VOXEL_DIM; z++) {
            uint16_t id = chunk.typeAt(x, y, z);
            if (id == BLOCK_AIR) continue;

            const float* col = BLOCK_COLORS[id < 6 ? id : 1];

            // Use neighbour sampler for edge faces
            if (sampleNeighbour(x, y+1, z) == BLOCK_AIR)
                addFace(x, y, z, 0, col);
            if (sampleNeighbour(x, y-1, z) == BLOCK_AIR)
                addFace(x, y, z, 1, col);
            if (sampleNeighbour(x, y, z+1) == BLOCK_AIR)
                addFace(x, y, z, 2, col);
            if (sampleNeighbour(x, y, z-1) == BLOCK_AIR)
                addFace(x, y, z, 3, col);
            if (sampleNeighbour(x-1, y, z) == BLOCK_AIR)
                addFace(x, y, z, 4, col);
            if (sampleNeighbour(x+1, y, z) == BLOCK_AIR)
                addFace(x, y, z, 5, col);
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
    }

private:
    std::vector<Vertex> vertices;
    GLuint vao = 0;
    GLuint vbo = 0;
    int vertexCount = 0;
    glm::vec3 offset = {0, 0, 0};

    void addFace(int x, int y, int z, int face, const float* col) {
        float shade = FACE_SHADE[face];
        float r = col[0] * shade;
        float g = col[1] * shade;
        float b = col[2] * shade;

        // Apply world offset so chunks sit at correct positions
        float fx = (float)x + offset.x;
        float fy = (float)y + offset.y;
        float fz = (float)z + offset.z;

        switch (face) {
            case 0: addQuad(fx,fy+1,fz, fx+1,fy+1,fz, fx+1,fy+1,fz+1, fx,fy+1,fz+1, r,g,b); break;
            case 1: addQuad(fx,fy,fz+1, fx+1,fy,fz+1, fx+1,fy,fz, fx,fy,fz, r,g,b); break;
            case 2: addQuad(fx+1,fy,fz+1, fx,fy,fz+1, fx,fy+1,fz+1, fx+1,fy+1,fz+1, r,g,b); break;
            case 3: addQuad(fx,fy,fz, fx+1,fy,fz, fx+1,fy+1,fz, fx,fy+1,fz, r,g,b); break;
            case 4: addQuad(fx,fy,fz+1, fx,fy,fz, fx,fy+1,fz, fx,fy+1,fz+1, r,g,b); break;
            case 5: addQuad(fx+1,fy,fz, fx+1,fy,fz+1, fx+1,fy+1,fz+1, fx+1,fy+1,fz, r,g,b); break;
        }
    }

    void addQuad(
        float x0,float y0,float z0,
        float x1,float y1,float z1,
        float x2,float y2,float z2,
        float x3,float y3,float z3,
        float r,float g,float b)
    {
        vertices.push_back({x0,y0,z0,r,g,b});
        vertices.push_back({x1,y1,z1,r,g,b});
        vertices.push_back({x2,y2,z2,r,g,b});
        vertices.push_back({x0,y0,z0,r,g,b});
        vertices.push_back({x2,y2,z2,r,g,b});
        vertices.push_back({x3,y3,z3,r,g,b});
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
            vertices.data(),
            GL_STATIC_DRAW);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE,
            sizeof(Vertex), (void*)offsetof(Vertex, x));
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE,
            sizeof(Vertex), (void*)offsetof(Vertex, r));
        glEnableVertexAttribArray(1);
        glBindVertexArray(0);
        vertexCount = (int)vertices.size();
    }
};