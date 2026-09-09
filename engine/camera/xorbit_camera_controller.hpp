#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <GLFW/glfw3.h>
#include <cstdint>

class XOrbitCameraController {
public:
    XOrbitCameraController(const float distance = 5.0f, const glm::vec3 focusPoint = glm::vec3(0.0f))
        : focusPoint(focusPoint), distance(distance), orientation(glm::quat(1, 0, 0, 0)), lastMousePos(0.0f), leftMouseDown(false), rightMouseDown(false),
          rotationSpeed(0.005f), panSpeed(0.01f), zoomSpeed(0.5f), revision(0u) {
        UpdateCameraVectors();
    }

    void Update(GLFWwindow* window) {
        double xpos, ypos;
        glfwGetCursorPos(window, &xpos, &ypos);
        const glm::vec2 mousePos(xpos, ypos);

        const int leftState = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT);
        const int rightState = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT);

        if (leftState == GLFW_PRESS) {
            if (leftMouseDown) {
                const glm::vec2 delta = mousePos - lastMousePos;
                if (delta.x != 0.0f || delta.y != 0.0f) {
                    const glm::quat yawQuat = glm::angleAxis(-delta.x * rotationSpeed, glm::vec3(0, 1, 0));
                    glm::quat const newOrientation = yawQuat * orientation;

                    const glm::vec3 right = newOrientation * glm::vec3(1, 0, 0);
                    const glm::quat pitchQuat = glm::angleAxis(-delta.y * rotationSpeed, right);

                    orientation = glm::normalize(pitchQuat * newOrientation);
                    UpdateCameraVectors();
                }
            }
            leftMouseDown = true;
        } else {
            leftMouseDown = false;
        }

        if (rightState == GLFW_PRESS) {
            if (rightMouseDown) {
                const glm::vec2 delta = mousePos - lastMousePos;
                if (delta.x != 0.0f || delta.y != 0.0f) {
                    const glm::vec3 right = orientation * glm::vec3(1, 0, 0);
                    const glm::vec3 up = orientation * glm::vec3(0, 1, 0);
                    const glm::vec3 pan = -right * delta.x * panSpeed * distance * 0.05f + up * delta.y * panSpeed * distance * 0.05f;

                    focusPoint += pan;
                    UpdateCameraVectors();
                }
            }
            rightMouseDown = true;
        } else {
            rightMouseDown = false;
        }

        lastMousePos = mousePos;
    }

    void OnScroll(const float yoffset) {
        const float newDistance = glm::max(0.1f, distance - yoffset * zoomSpeed);
        if (newDistance != distance) {
            distance = newDistance;
            UpdateCameraVectors();
        }
    }

    glm::mat4 GetViewMatrix() const {
        const glm::mat4 rotation = glm::mat4_cast(glm::conjugate(orientation));
        const glm::mat4 translation = glm::translate(glm::mat4(1.0f), -position);
        return rotation * translation;
    }

    glm::vec3 GetPosition() const {
        return position;
    }
    glm::vec3 GetFocusPoint() const {
        return focusPoint;
    }
    float GetDistance() const {
        return distance;
    }
    uint64_t GetRevision() const {
        return revision;
    }
    bool IsInteracting() const {
        return leftMouseDown || rightMouseDown;
    }

private:
    void UpdateCameraVectors() {
        const glm::vec3 forward = orientation * glm::vec3(0, 0, -1);
        position = focusPoint - forward * distance;
        revision++;
    }

    glm::vec3 position;
    glm::vec3 focusPoint;
    float distance;
    glm::quat orientation;

    glm::vec2 lastMousePos;
    bool leftMouseDown;
    bool rightMouseDown;

    float rotationSpeed;
    float panSpeed;
    float zoomSpeed;
    uint64_t revision;
};
