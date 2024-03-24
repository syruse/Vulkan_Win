#pragma once

#include <glm/gtc/quaternion.hpp>
#include "VulkanState.h"

class Camera {
public:
    constexpr static float ANGLE_GAIN = 0.25f;
    constexpr static float GAIN_MOVEMENT = 0.25f;

    enum class EDirection { Forward = 0, Left, Right, Back };

    struct Perstective {
        float fovy = 65.0f;
        float aspect = 1.0f;  // width / height
        float near_ = 0.01f;
        float far_ = 1000.0f;
    };

    struct ViewProj {
        glm::mat4 view;
        glm::mat4 proj;
    };

    Camera(const Perstective& perstective, const glm::vec3& eye, const glm::vec3& target = glm::vec3(0.0f, 0.0f, 0.0f));
    void resetPerspective(const Perstective& perstective);

    const ViewProj& viewProjMat() {
        return mViewProj;
    }

    glm::mat4 targetModelMat() {
        return glm::translate(glm::mat4(1.0f), mTarget) * glm::mat4_cast(mEndCameraRotation);
    }

    const glm::vec3& targetPos() {
        return mTarget;
    }

    glm::vec3 cameraPosition() {
        return mTarget + mFromTargetToEye;
    }

    bool isInterpolationFinished() {
        return mInterpolationK == 1.0f;
    }

    void update(float deltaTime, bool withSmoothTransition = true);
    void move(EDirection dir);

private:
    glm::vec3 mTarget{0.0f, 0.0f, 0.0f};
    glm::vec3 mFromTargetToEye{0.0f, 0.0f, 0.0f};
    ViewProj mViewProj{};
    glm::quat mStartCameraRotation{};
    glm::quat mEndCameraRotation{};
    float mInterpolationK{0.0f};
};
