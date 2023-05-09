#pragma once

#include "VulkanState.h"
#include <glm/gtc/quaternion.hpp>

class Camera {
public:
    constexpr static float ANGLE_GAIN = 3.0f;
    constexpr static float GAIN_MOVEMENT = 1.0f;

    enum class EDirection {
        Forward = 0,
        Left,
        Right,
        Back
    };
    struct Perstective {
        float fovy = 65.0f;
        float aspect = 1.0f; // width / height
        float near_ = 0.01f;
        float far_ = 1000.0f;
    };
    Camera(const Perstective& perstective, const glm::vec3& eye, const glm::vec3& target = glm::vec3(0.0f, 0.0f, 0.0f));
    void resetPerspective(const Perstective& perstective);

    const VulkanState::ViewProj& viewProjMat() {
        return mViewProj;
    }

    glm::mat4 targetModelMat() {
        return glm::translate(glm::mat4(1.0f), mTarget) * glm::mat4_cast(mEndCameraRotation);
    }

    void update(float deltaTime);
    void move(EDirection dir);

private:
    glm::vec3 mTarget{0.0f, 0.0f, 0.0f};
    glm::vec3 mFromTargetToEye{0.0f, 0.0f, 0.0f};
    VulkanState::ViewProj mViewProj{};
    glm::quat mStartCameraRotation{};
    glm::quat mEndCameraRotation{};
    float mInterpolationK{0.0f};
};
