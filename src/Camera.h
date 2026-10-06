#pragma once

#include <glm/glm.hpp>

class Camera {
public:
    Camera();

    // Accessors
    const glm::vec3& position() const { return position_; }
    void setPosition(const glm::vec3& p) { position_ = p; }

    float yaw() const { return yaw_; }
    float pitch() const { return pitch_; }
    void setYawPitch(float yaw, float pitch);

    // Matrices
    glm::mat4 getView() const;
    glm::mat4 getProj(float fovDeg, float aspect, float nearZ, float farZ) const;

    // Directions
    glm::vec3 forward() const;
    glm::vec3 right() const;

private:
    glm::vec3 position_;
    float yaw_;
    float pitch_;
};
