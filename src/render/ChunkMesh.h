#pragma once
#include <glad/glad.h>
#include <cstddef>
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
        , cutoutCount(o.cutoutCount)
    { o.vao = 0; o.vbo = 0; o.opaqueCount = 0; o.cutoutCount = 0; }

    ChunkMesh& operator=(ChunkMesh&& o) noexcept {
        if (this != &o) {
            cleanup();
            vao = o.vao; vbo = o.vbo;
            opaqueCount = o.opaqueCount;
            cutoutCount = o.cutoutCount;
            o.vao = 0; o.vbo = 0;
            o.opaqueCount = 0; o.cutoutCount = 0;
        }
        return *this;
    }

    void upload(const ChunkMeshData& data) {
        opaqueCount = (GLsizei)data.opaqueCount;
        cutoutCount = (GLsizei)(data.vertices.size()
                                - data.opaqueCount);

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

    // Solid blocks — draw with back-face culling on
    void drawOpaque() const {
        if (vao == 0 || opaqueCount == 0) return;
        glBindVertexArray(vao);
        glDrawArrays(GL_TRIANGLES, 0, opaqueCount);
    }

    // Leaves and other cut-out blocks — draw two-sided
    void drawCutout() const {
        if (vao == 0 || cutoutCount == 0) return;
        glBindVertexArray(vao);
        glDrawArrays(GL_TRIANGLES, opaqueCount, cutoutCount);
    }

    void cleanup() {
        releaseBuffers();
        opaqueCount = 0;
        cutoutCount = 0;
    }

    bool    isEmpty()     const { return opaqueCount + cutoutCount == 0; }
    bool    hasCutout()   const { return cutoutCount > 0; }
    GLsizei vertexCount() const { return opaqueCount + cutoutCount; }

private:
    GLuint  vao = 0, vbo = 0;
    GLsizei opaqueCount = 0;
    GLsizei cutoutCount = 0;

    void releaseBuffers() {
        if (vao) { glDeleteVertexArrays(1, &vao); vao = 0; }
        if (vbo) { glDeleteBuffers(1, &vbo);      vbo = 0; }
    }
};
