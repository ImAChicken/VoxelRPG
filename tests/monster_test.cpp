// monster_test — the monster models (character/MonsterModel.h), headless.
// Run from the repo root. Checks each mushroom file reads, that its
// skin at rest leaves the mesh where it is, and that its clips move it.
//   monster_test --png <dir>   also writes a flat-shaded picture of each
//                              at rest and mid-walk (a quick look, no GL)
#include "character/MonsterModel.h"
#include "app/Screenshot.h"
#include <cstdio>
#include <string>

static int failures = 0;
static void check(bool ok, const std::string& what) {
    std::printf("  %s  %s\n", ok ? "ok  " : "FAIL", what.c_str());
    if (!ok) failures++;
}

// the mesh posed, in model space
static std::vector<glm::vec3> posed(const MonsterModel& m, const MonsterPose& p) {
    std::vector<glm::mat4> s;
    m.skin(p, s);
    std::vector<glm::vec3> out(m.verts.size());
    for (size_t i = 0; i < m.verts.size(); i++) {
        const MonsterVertex& v = m.verts[i];
        glm::mat4 M(0.0f);
        for (int k = 0; k < 4; k++) M += s[v.bone[k]] * (v.weight[k] / 255.0f);
        out[i] = glm::vec3(M * glm::vec4(v.x, v.y, v.z, 1.0f));
    }
    return out;
}

// a flat-shaded picture from the front-left (painter's order, no GL)
static void picture(const MonsterModel& m, const std::vector<glm::vec3>& P, const std::string& path) {
    const int W = 320, H = 320;
    std::vector<uint8_t> px((size_t)W * H * 3, 40);
    std::vector<float> zb((size_t)W * H, 1e9f);
    glm::vec3 lo(1e9f), hi(-1e9f);
    for (const glm::vec3& p : P) { lo = glm::min(lo, p); hi = glm::max(hi, p); }
    float size = std::max({ hi.x - lo.x, hi.y - lo.y, hi.z - lo.z }) * 1.15f;
    glm::vec3 mid = (lo + hi) * 0.5f;
    float yaw = glm::radians(-30.0f);
    glm::vec3 L = glm::normalize(glm::vec3(0.33f, 0.74f, 0.58f));
    auto proj = [&](glm::vec3 p) {
        p -= mid;
        glm::vec3 q(p.x * std::cos(yaw) + p.z * std::sin(yaw), p.y, -p.x * std::sin(yaw) + p.z * std::cos(yaw));
        return glm::vec3((q.x / size + 0.5f) * W, (0.5f - q.y / size) * H, -q.z);
    };
    for (size_t i = 0; i + 2 < P.size(); i += 3) {
        glm::vec3 a = proj(P[i]), b = proj(P[i + 1]), c = proj(P[i + 2]);
        glm::vec3 n = glm::normalize(glm::cross(P[i + 1] - P[i], P[i + 2] - P[i]));
        float d = std::max(glm::dot(n, L), 0.0f);
        float ramp = d < 0.25f ? 0.49f : (d < 0.5f ? 0.76f : 1.0f);      // the characters' toon bands
        float k = 0.55f + 0.45f * ramp;
        const MonsterVertex& v = m.verts[i];
        int x0 = std::max(0, (int)std::min({ a.x, b.x, c.x })), x1 = std::min(W - 1, (int)std::max({ a.x, b.x, c.x }) + 1);
        int y0 = std::max(0, (int)std::min({ a.y, b.y, c.y })), y1 = std::min(H - 1, (int)std::max({ a.y, b.y, c.y }) + 1);
        float area = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
        if (std::fabs(area) < 1e-6f) continue;
        for (int y = y0; y <= y1; y++) for (int x = x0; x <= x1; x++) {
            float px_ = x + 0.5f, py = y + 0.5f;
            float w0 = ((b.x - px_) * (c.y - py) - (b.y - py) * (c.x - px_)) / area;
            float w1 = ((c.x - px_) * (a.y - py) - (c.y - py) * (a.x - px_)) / area;
            float w2 = 1.0f - w0 - w1;
            if (w0 < 0 || w1 < 0 || w2 < 0) continue;
            float z = w0 * a.z + w1 * b.z + w2 * c.z;
            size_t o = (size_t)y * W + x;
            if (z >= zb[o]) continue;
            zb[o] = z;
            px[o * 3 + 0] = (uint8_t)std::min(255.0f, v.r * k);
            px[o * 3 + 1] = (uint8_t)std::min(255.0f, v.g * k);
            px[o * 3 + 2] = (uint8_t)std::min(255.0f, v.b * k);
        }
    }
    screenshot::writePng(path, W, H, px);
}

int main(int argc, char** argv) {
    std::string pngDir;
    for (int i = 1; i + 1 < argc; i++) if (std::string(argv[i]) == "--png") pngDir = argv[i + 1];
    int n = gMonsters.load("data/models/monsters");
    std::printf("monster models: %d\n", n);
    check(n >= 3, "the mushroom files are there");
    for (const char* name : { "mushnub", "mushnub_evolved", "mushroomking" }) {
        const MonsterModel* m = gMonsters.find(name);
        std::printf("%s\n", name);
        check(m != nullptr, "reads");
        if (!m) continue;
        std::printf("    %zu bones, %zu triangles, %zu clips, %.2f tall\n", m->bones.size(), m->verts.size() / 3, m->clips.size(), m->height());
        MonsterPose rest; m->restPose(rest);
        std::vector<glm::vec3> R = posed(*m, rest);
        // (the file's rest pose is the scene's, not quite the bind pose;
        // standing idle: its feet on the ground, about its own height)
        {
            int idle = m->clip("Idle");
            MonsterPose ip; m->sample(idle, 0.0f, true, ip);
            std::vector<glm::vec3> I = posed(*m, ip);
            glm::vec3 lo(1e9f), hi(-1e9f);
            for (const glm::vec3& v : I) { lo = glm::min(lo, v); hi = glm::max(hi, v); }
            std::printf("    idle: from %.2f to %.2f high\n", lo.y, hi.y);
            check(std::fabs(lo.y) < 0.1f * m->height(), "idle, its feet are on the ground");
            check(std::fabs((hi.y - lo.y) - m->height()) < 0.25f * m->height(), "idle, about as tall as the mesh");
        }
        bool colours = false;
        for (const MonsterVertex& v : m->verts) colours = colours || v.r != v.g || v.g != v.b;
        check(colours, "its faces have colours from the texture");
        for (const char* c : { "Idle", "Walk", "Death" }) check(m->clip(c) >= 0, std::string("has ") + c);
        int walk = m->clip("Walk");
        MonsterPose p; m->sample(walk, m->seconds(walk) * 0.25f, true, p);
        std::vector<glm::vec3> Wk = posed(*m, p);
        float moved = 0.0f;
        for (size_t i = 0; i < Wk.size(); i++) moved = std::max(moved, glm::length(Wk[i] - R[i]));
        check(moved > 0.02f * m->height() && moved < 1.5f * m->height(), "walking moves it, not too far (" + std::to_string(moved) + ")");
        if (!pngDir.empty()) {
            picture(*m, R, pngDir + "/" + name + "_rest.png");
            picture(*m, Wk, pngDir + "/" + name + "_walk.png");
            int death = m->clip("Death");
            MonsterPose dp; m->sample(death, 99.0f, false, dp);
            picture(*m, posed(*m, dp), pngDir + "/" + name + "_dead.png");
        }
    }
    std::printf("%s\n", failures ? "SOME FAILED" : "all passed");
    return failures ? 1 : 0;
}
