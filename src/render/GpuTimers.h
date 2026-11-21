#pragma once
// ══════════════════════════════════════════════════════════
// GpuTimers — how long the GPU spends on each pass (F3)
// ══════════════════════════════════════════════════════════
// A timestamp is written after each pass (mark), and the time between
// two marks is the pass ending at the second one. Read back three
// frames later (no waiting on the GPU), smoothed. Only while the debug
// overlay is on: nothing is written otherwise.
//
//   timers.beginFrame(on);  ...  timers.mark("world");  ...  timers.endFrame();
//   timers.lines()          "world 2.1 ms" for each pass, then the frame

#include <glad/glad.h>
#include <cstdio>
#include <string>
#include <vector>

class GpuTimers {
public:
    static constexpr int MAX = 16, RING = 3;

    void beginFrame(bool on) {
        active = on;
        if (!active) return;
        if (!made) { glGenQueries(RING * MAX, &q[0][0]); made = true; }
        n[slot] = 0;
        mark("start");
    }
    void mark(const char* name) {
        if (!active || n[slot] >= MAX) return;
        glQueryCounter(q[slot][n[slot]], GL_TIMESTAMP);
        names[slot][n[slot]] = name;
        n[slot]++;
    }
    void endFrame() {
        if (!active) return;
        mark("end");
        slot = (slot + 1) % RING;
        read(slot);                                    // (the oldest: written RING - 1 frames ago)
    }
    std::vector<std::string> lines() const {
        std::vector<std::string> out;
        char b[96];
        float total = 0.0f;
        std::string row;
        for (size_t i = 0; i < passes.size(); i++) {
            total += passes[i].ms;
            std::snprintf(b, sizeof(b), "%s %.2f", passes[i].name.c_str(), passes[i].ms);
            row += (row.empty() ? "" : "  ") + std::string(b);
            if (row.size() > 60) { out.push_back(row); row.clear(); }
        }
        if (!row.empty()) out.push_back(row);
        std::snprintf(b, sizeof(b), "gpu %.2f ms a frame", total);
        out.insert(out.begin(), b);
        return out;
    }

private:
    struct Pass { std::string name; float ms = 0.0f; };
    GLuint q[RING][MAX] = {};
    const char* names[RING][MAX] = {};
    int n[RING] = {};
    int slot = 0;
    bool made = false, active = false;
    std::vector<Pass> passes;

    void read(int s) {
        if (n[s] < 2) return;
        GLint ready = 0;
        glGetQueryObjectiv(q[s][n[s] - 1], GL_QUERY_RESULT_AVAILABLE, &ready);
        if (!ready) return;
        GLuint64 t[MAX];
        for (int i = 0; i < n[s]; i++) glGetQueryObjectui64v(q[s][i], GL_QUERY_RESULT, &t[i]);
        for (int i = 1; i < n[s]; i++) {
            std::string name = names[s][i];
            float ms = (float)((double)(t[i] - t[i - 1]) / 1.0e6);
            Pass* p = nullptr;
            for (Pass& x : passes) if (x.name == name) p = &x;
            if (!p) { passes.push_back({ name, ms }); continue; }
            p->ms += (ms - p->ms) * 0.1f;              // (smoothed)
        }
        n[s] = 0;
    }
};
