#include "Camera.h"

namespace {
glm::vec3 _forwardDir = glm::vec3(0.0f, 0.0f, 1.0f);
glm::vec3 _leftDir = glm::vec3(-1.0f, 0.0f, 0.0f);
glm::vec3 _upDir = glm::vec3(0.0f, 1.0f, 0.0f);
};  // namespace

Camera::Camera(const Perstective& perstective, const glm::vec3& eye, const glm::vec3& target)
    : m_Perstpective(perstective), mTarget(target) {
    mFromTargetToEye = eye - target;
    mCurrentFromTargetToEye = mFromTargetToEye;
    mViewProj.view = glm::lookAt(eye, target, _upDir);
    mStartCameraRotation = glm::angleAxis(glm::radians(0.0f), _upDir);
    mEndCameraRotation = mStartCameraRotation;
    mCurrentCameraRotation = mStartCameraRotation;
    resetPerspective(perstective);
}

void Camera::resetPerspective(const Perstective& perstective) {
    m_Perstpective = perstective;
    mViewProj.proj = glm::perspective(glm::radians(perstective.fovy), perstective.aspect, perstective.near_, perstective.far_);

    /**
     * GLM was originally designed for OpenGL, where the Y coordinate of the clip coordinates is inverted
     * The easiest way to compensate for that is to flip the sign on the scaling factor of the Y axis in the projection matrix
     */
    mViewProj.proj[1][1] *= -1;
}

void Camera::update(float deltaTime, bool withSmoothTransition) {
    static constexpr float ROTATION_DURATION_MS = 500.0f;

    if (withSmoothTransition) {
        // stable rotation speed at any frame rate, no dependency on deltaTime scale
        float deltaK = (ROTATION_DURATION_MS > 0.0f) ? (deltaTime / ROTATION_DURATION_MS) : 1.0f;
        mInterpolationK = std::min(1.0f, mInterpolationK + deltaK);
    } else {
        mInterpolationK = 1.0f;
    }

    // normalize the quaternion to prevent accumulation of numerical error
    mCurrentCameraRotation = glm::normalize(glm::mix(mStartCameraRotation, mEndCameraRotation, mInterpolationK));

    // Apply the rotation to the vector from the target to the camera.
    // (slerp ensures smooth angular velocity, stabilizing motion vectors)
    mCurrentFromTargetToEye = mCurrentCameraRotation * mFromTargetToEye;

    mViewProj.view = glm::lookAt(mTarget + mCurrentFromTargetToEye, mTarget, _upDir);
}

void Camera::move(EDirection dir) {
    static const glm::quat rotRight = glm::angleAxis(glm::radians((-1.0f * ANGLE_GAIN)), _upDir);
    static const glm::quat rotLeft = glm::angleAxis(glm::radians(ANGLE_GAIN), _upDir);

    if (dir == EDirection::Left || dir == EDirection::Right) {
        mStartCameraRotation = mEndCameraRotation;
        mInterpolationK = 0.0f;
        mEndCameraRotation = ((dir == EDirection::Left) ? rotLeft : rotRight) * mEndCameraRotation;
    }

    glm::vec3 rotDir = mEndCameraRotation * _forwardDir;
    mTarget += GAIN_MOVEMENT * ((dir == EDirection::Back) ? (rotDir * -1.0f) : rotDir);

    // fallback applying changes for Forward\Back direction if update called before move
    update(0.0f);

    return;
}
