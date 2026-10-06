#include "Camera.h"
#include <glm/gtc/matrix_transform.hpp>

Camera::Camera()
    : position_(0.0f, 6.0f, 12.0f), yaw_(-90.0f), pitch_(-15.0f) {}

void Camera::setYawPitch(float yaw, float pitch) {
    yaw_ = yaw;
    // Clamp pitch to avoid gimbal lock
    if (pitch > 89.0f) pitch = 89.0f;
    if (pitch < -89.0f) pitch = -89.0f;
    pitch_ = pitch;
}

glm::vec3 Camera::forward() const {
    glm::vec3 f(
        cos(glm::radians(yaw_)) * cos(glm::radians(pitch_)),
        sin(glm::radians(pitch_)),
        sin(glm::radians(yaw_)) * cos(glm::radians(pitch_))
    );
    return glm::normalize(f);
}

glm::vec3 Camera::right() const {
    return glm::normalize(glm::cross(forward(), glm::vec3(0,1,0)));
}

glm::mat4 Camera::getView() const {
    return glm::lookAt(position_, position_ + forward(), glm::vec3(0,1,0));
}

glm::mat4 Camera::getProj(float fovDeg, float aspect, float nearZ, float farZ) const {
    return glm::perspective(glm::radians(fovDeg), aspect, nearZ, farZ);
}
