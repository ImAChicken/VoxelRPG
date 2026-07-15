#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

class Camera {
public:
    glm::vec3 position  = { 32.0f, 50.0f, 32.0f };
    glm::vec3 front     = { 0.0f,  0.0f, -1.0f  };
    glm::vec3 up        = { 0.0f,  1.0f,  0.0f  };

    float yaw           = -90.0f;
    float pitch         =   0.0f;
    float moveSpeed     =  20.0f;
    float sensitivity   =   0.1f;
    bool  firstMouse    = true;

    glm::mat4 getViewMatrix() const {
        return glm::lookAt(position,
                           position + front, up);
    }

    glm::mat4 getProjectionMatrix(
            float aspectRatio) const {
        return glm::perspective(
            glm::radians(70.0f),
            aspectRatio, 0.1f, 500.0f);
    }

    // Called with pre-computed delta from InputManager
    // xoffset and yoffset are pixel deltas this frame
    void processMouse(float xoffset, float yoffset) {
        if (firstMouse) {
            firstMouse = false;
            return;
        }

        yaw   += xoffset * sensitivity;
        pitch -= yoffset * sensitivity;  // ← change += to -=

        if (pitch >  89.0f) pitch =  89.0f;
        if (pitch < -89.0f) pitch = -89.0f;

        updateFront();
    }

    void processKeyboard(int forward, int backward,
                         int left,    int right,
                         int up_key,  int down_key,
                         float deltaTime) {
        float velocity = moveSpeed * deltaTime;
        glm::vec3 r = glm::normalize(
            glm::cross(front, up));

        if (forward)  position += front * velocity;
        if (backward) position -= front * velocity;
        if (left)     position -= r     * velocity;
        if (right)    position += r     * velocity;
        if (up_key)   position += up    * velocity;
        if (down_key) position -= up    * velocity;
    }

private:
    void updateFront() {
        glm::vec3 dir;
        dir.x = cos(glm::radians(yaw)) *
                cos(glm::radians(pitch));
        dir.y = sin(glm::radians(pitch));
        dir.z = sin(glm::radians(yaw)) *
                cos(glm::radians(pitch));
        front = glm::normalize(dir);
    }
};