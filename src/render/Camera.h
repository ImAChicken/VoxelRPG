#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

class Camera {
public:
    glm::vec3 position  = { 32.0f, 50.0f, 32.0f }; // start above chunk center
    glm::vec3 front     = { 0.0f,  -0.5f, -1.0f };  // looking down and forward
    glm::vec3 up        = { 0.0f,   1.0f,  0.0f };

    float yaw           = -90.0f;   // horizontal angle
    float pitch         = -25.0f;   // vertical angle — looking slightly down
    float moveSpeed     = 20.0f;    // voxels per second
    float sensitivity   = 0.1f;     // mouse sensitivity

    bool firstMouse     = true;
    float lastX         = 640.0f;
    float lastY         = 360.0f;

    glm::mat4 getViewMatrix() const {
        return glm::lookAt(position, position + front, up);
    }

    glm::mat4 getProjectionMatrix(float aspectRatio) const {
        return glm::perspective(
            glm::radians(70.0f),
            aspectRatio,
            0.1f,
            500.0f
        );
    }

    void processKeyboard(int forward, int backward,
                         int left,    int right,
                         int up_key,  int down_key,
                         float deltaTime) {
        float velocity = moveSpeed * deltaTime;
        if (forward)  position += front * velocity;
        if (backward) position -= front * velocity;
        if (left)     position -= glm::normalize(
                                    glm::cross(front, up)) * velocity;
        if (right)    position += glm::normalize(
                                    glm::cross(front, up)) * velocity;
        if (up_key)   position += up * velocity;
        if (down_key) position -= up * velocity;
    }

    void processMouse(float xpos, float ypos) {
        if (firstMouse) {
            lastX = xpos;
            lastY = ypos;
            firstMouse = false;
        }

        float xoffset = (xpos - lastX) * sensitivity;
        float yoffset = (lastY - ypos) * sensitivity; // reversed — y goes bottom to top
        lastX = xpos;
        lastY = ypos;

        yaw   += xoffset;
        pitch += yoffset;

        // Clamp pitch so we don't flip upside down
        if (pitch >  89.0f) pitch =  89.0f;
        if (pitch < -89.0f) pitch = -89.0f;

        updateFront();
    }

private:
    void updateFront() {
        glm::vec3 dir;
        dir.x = cos(glm::radians(yaw)) * cos(glm::radians(pitch));
        dir.y = sin(glm::radians(pitch));
        dir.z = sin(glm::radians(yaw)) * cos(glm::radians(pitch));
        front = glm::normalize(dir);
    }
};