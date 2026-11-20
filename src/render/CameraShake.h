#pragma once
// Camera shake: "trauma" that hits add and time drains; the shake is
// trauma² (a small knock barely moves it, a big one jolts), from smooth
// noise so it wobbles rather than jitters. Applied to a copy of the
// camera just before drawing, so the real look direction never drifts.
// Settings > General > Camera shake scales it (0 off … 2 double).

#include "game/Settings.h"
#include "render/Camera.h"
#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>

class CameraShake {
public:
    static constexpr float DRAIN     = 1.8f;    // trauma lost per second
    static constexpr float MAX_YAW   = 2.2f;    // degrees at full trauma
    static constexpr float MAX_PITCH = 1.6f;
    static constexpr float MAX_MOVE  = 0.45f;   // voxels

    // a knock: 0.1 a light hit landed, 0.3 a heavy, 0.5 a big one
    void add(float amount) { trauma = std::min(1.0f, trauma + std::max(0.0f, amount)); }
    void update(float dt) { time += dt; trauma = std::max(0.0f, trauma - DRAIN * dt); }
    void clear() { trauma = 0.0f; }
    float now() const { return trauma; }          // (the debug view)

    // the camera to draw with: shaken
    Camera apply(const Camera& c) const {
        float k = trauma * trauma * std::clamp(gSettings.cameraShake, 0.0f, 2.0f);
        if (k <= 1e-4f) return c;
        Camera s = c;
        auto n = [&](float f, float p) { return std::sin(time * f + p) * 0.6f + std::sin(time * f * 2.3f + p * 1.7f) * 0.4f; };
        s.setLook(c.yaw + MAX_YAW * k * n(31.0f, 0.0f), c.pitch + MAX_PITCH * k * n(27.0f, 1.3f));
        s.position += glm::vec3(n(23.0f, 2.1f), n(29.0f, 0.7f), n(19.0f, 4.2f)) * (MAX_MOVE * k);
        return s;
    }

private:
    float trauma = 0.0f, time = 0.0f;
};

inline CameraShake gCamShake;
