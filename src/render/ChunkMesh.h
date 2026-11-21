#pragma once
#include <glad/glad.h>
#include <cstddef>
#include <algorithm>
#include <glm/glm.hpp>
#include "render/Mesher.h"

// ── ChunkMesh — one chunk's triangles on the GPU ──────────
// The CPU-side ChunkMeshData is built on a worker thread by
// ChunkMesher. upload() copies it into a vertex buffer and
// must be called on the main thread (the one that owns the
// OpenGL context).
class ChunkMesh {
public:
    ChunkMesh() = default;
    ~ChunkMesh() { cleanup(); }

    ChunkMesh(const ChunkMesh&)            = delete;
    ChunkMesh& operator=(const ChunkMesh&) = delete;

    ChunkMesh(ChunkMesh&& o) noexcept
        : vao(o.vao), vbo(o.vbo)
        , opaqueCount(o.opaqueCount)
        , cutoutCount(o.cutoutCount), paneCount(o.paneCount)
    { std::copy(o.faceStart, o.faceStart + 7, faceStart); o.vao = 0; o.vbo = 0; o.opaqueCount = 0; o.cutoutCount = 0; o.paneCount = 0; }

    ChunkMesh& operator=(ChunkMesh&& o) noexcept {
        if (this != &o) {
            cleanup();
            vao = o.vao; vbo = o.vbo;
            opaqueCount = o.opaqueCount;
            cutoutCount = o.cutoutCount;
            paneCount = o.paneCount;
            std::copy(o.faceStart, o.faceStart + 7, faceStart);
            o.vao = 0; o.vbo = 0;
            o.opaqueCount = 0; o.cutoutCount = 0; o.paneCount = 0;
        }
        return *this;
    }

    void upload(const ChunkMeshData& data) {
        opaqueCount = (GLsizei)data.opaqueCount;
        for (int f = 0; f < 7; f++) faceStart[f] = (GLint)data.faceStart[f];
        if (faceStart[6] != opaqueCount) { for (int f = 0; f < 6; f++) faceStart[f] = 0; faceStart[6] = opaqueCount; }   // (no ranges: all one)
        paneCount = (GLsizei)data.paneCount;
        cutoutCount = (GLsizei)(data.vertices.size()
                                - data.opaqueCount - data.paneCount);

        if (data.vertices.empty()) {
            // Nothing to draw — release any old buffers
            releaseBuffers();
            return;
        }

        if (vao == 0) {
            glGenVertexArrays(1, &vao);
            glGenBuffers(1, &vbo);
        }
        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER,
            (GLsizeiptr)(data.vertices.size() * sizeof(ChunkVertex)),
            data.vertices.data(), GL_STATIC_DRAW);

        // location 0 — x, y, z, face   (4 unsigned bytes)
        // location 1 — tileX, tileY    (+ 2 padding bytes)
        // The "I" in glVertexAttribIPointer keeps them as
        // integers in the shader instead of converting to float
        glVertexAttribIPointer(0, 4, GL_UNSIGNED_BYTE,
            sizeof(ChunkVertex),
            (void*)offsetof(ChunkVertex, x));
        glEnableVertexAttribArray(0);

        glVertexAttribIPointer(1, 4, GL_UNSIGNED_BYTE,
            sizeof(ChunkVertex),
            (void*)offsetof(ChunkVertex, tileX));
        glEnableVertexAttribArray(1);

        glBindVertexArray(0);
    }

    // Solid blocks — draw with back-face culling on. cam: the camera
    // from the section's corner (vox): a direction whose faces all
    // face away from it is skipped (Face order: +y -y +z -z -x +x).
    // Returns the vertices drawn.
    GLsizei drawOpaque(glm::vec3 cam = glm::vec3(64.0f)) const {
        if (vao == 0 || opaqueCount == 0) return 0;
        const float S = 128.0f;                                 // (VOXEL_DIM: a section's size)
        const bool see[6] = { cam.y > 0.0f, cam.y < S, cam.z > 0.0f, cam.z < S, cam.x < S, cam.x > 0.0f };
        GLint first[6]; GLsizei count[6]; int n = 0; GLsizei drawn = 0;
        for (int f = 0; f < 6; f++) {
            GLsizei c = faceStart[f + 1] - faceStart[f];
            if (!see[f] || c <= 0) continue;
            if (n > 0 && first[n - 1] + count[n - 1] == faceStart[f]) count[n - 1] += c;     // (next to the last: one run)
            else { first[n] = faceStart[f]; count[n] = c; n++; }
            drawn += c;
        }
        if (n == 0) return 0;
        glBindVertexArray(vao);
        if (n == 1) glDrawArrays(GL_TRIANGLES, first[0], count[0]);
        else glMultiDrawArrays(GL_TRIANGLES, first, count, n);
        return drawn;
    }

    // Leaves and other cut-out blocks — draw two-sided
    void drawCutout() const {
        if (vao == 0 || cutoutCount == 0) return;
        glBindVertexArray(vao);
        glDrawArrays(GL_TRIANGLES, opaqueCount, cutoutCount);
    }

    // Window glass: see-through sheets, drawn after all solid chunks
    void drawPanes() const {
        if (vao == 0 || paneCount == 0) return;
        glBindVertexArray(vao);
        glDrawArrays(GL_TRIANGLES, opaqueCount + cutoutCount, paneCount);
    }

    void cleanup() {
        releaseBuffers();
        opaqueCount = 0;
        cutoutCount = 0;
        paneCount = 0;
    }

    bool    isEmpty()     const { return opaqueCount + cutoutCount + paneCount == 0; }
    bool    hasCutout()   const { return cutoutCount > 0; }
    bool    hasPanes()    const { return paneCount > 0; }
    GLsizei vertexCount() const { return opaqueCount + cutoutCount + paneCount; }
    GLsizei opaqueVertices() const { return opaqueCount; }

private:
    GLuint  vao = 0, vbo = 0;
    GLsizei opaqueCount = 0;
    GLsizei cutoutCount = 0;
    GLsizei paneCount = 0;
    GLint   faceStart[7] = { 0, 0, 0, 0, 0, 0, 0 };

    void releaseBuffers() {
        if (vao) { glDeleteVertexArrays(1, &vao); vao = 0; }
        if (vbo) { glDeleteBuffers(1, &vbo);      vbo = 0; }
    }
};
