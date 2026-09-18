#pragma once

#include <glm/gtc/quaternion.hpp>
#include "VulkanState.h"

class Camera {
public:
    // Values are per-reference-frame (see REFERENCE_FRAME_MS); move() scales them by
    // deltaTime so tank speed stays constant across frame rates.
    // now that speed is frame-rate independent.
    constexpr static float ANGLE_GAIN = 0.75f;
    constexpr static float GAIN_MOVEMENT = 0.75f;
    constexpr static float REFERENCE_FRAME_MS = 1000.0f / 60.0f;
    // Cap the per-call scale so a stutter/lag spike can't teleport the tank through geometry.
    constexpr static float MAX_FRAME_SCALE = 4.0f;

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
    virtual void resetPerspective(const Perstective& perstective);

    const Perstective& getPerspective() const { return m_Perstpective; }

    virtual const ViewProj& viewProjMat() {
        return mViewProj;
    }

    glm::mat4 targetModelMat() {
        return glm::translate(glm::mat4(1.0f), mTarget) * glm::mat4_cast(mCurrentCameraRotation);
    }

    const glm::vec3& targetPos() {
        return mTarget;
    }

    virtual glm::vec3 cameraPosition() {
        return mTarget + mCurrentFromTargetToEye;
    }

    bool isInterpolationFinished() {
        return mInterpolationK == 1.0f;
    }

    virtual void update(float deltaTime, bool withSmoothTransition = true);
    // deltaTimeMs makes the per-call rotation/translation frame-rate independent.
    // speedMultiplier scales the forward/back translation distance (used for sprint).
    // applyTranslation=false performs a pure turn-in-place (no forward creep), used when
    // Forward/Back is already translating the tank this frame to avoid stacking movement.
    virtual void move(EDirection dir, float deltaTimeMs, float speedMultiplier = 1.0f, bool applyTranslation = true);

private:
    Perstective m_Perstpective;
    glm::vec3 mTarget{0.0f, 0.0f, 0.0f};
    glm::vec3 mFromTargetToEye{0.0f, 0.0f, 0.0f};
    ViewProj mViewProj{};
    glm::quat mStartCameraRotation{};
    glm::quat mEndCameraRotation{};
    glm::quat mCurrentCameraRotation{};
    glm::vec3 mCurrentFromTargetToEye{0.0f, 0.0f, 0.0f};
    float mInterpolationK{0.0f};
};
