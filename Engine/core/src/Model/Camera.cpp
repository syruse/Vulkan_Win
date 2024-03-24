#include "Camera.h"

namespace {
glm::vec3 _forwardDir = glm::vec3(0.0f, 0.0f, 1.0f);
glm::vec3 _leftDir = glm::vec3(-1.0f, 0.0f, 0.0f);
glm::vec3 _upDir = glm::vec3(0.0f, 1.0f, 0.0f);
};  // namespace

Camera::Camera(const Perstective& perstective, const glm::vec3& eye, const glm::vec3& target) : mTarget(target) {
    mFromTargetToEye = eye - target;
    mViewProj.view = glm::lookAt(eye, target, _upDir);
    mStartCameraRotation = glm::angleAxis(glm::radians(0.0f), _upDir);
    mEndCameraRotation = mStartCameraRotation;
    resetPerspective(perstective);
}

void Camera::resetPerspective(const Perstective& perstective) {
    mViewProj.proj = glm::perspective(glm::radians(perstective.fovy), perstective.aspect, perstective.near_, perstective.far_);

    /**
     * GLM was originally designed for OpenGL, where the Y coordinate of the clip coordinates is inverted
     * The easiest way to compensate for that is to flip the sign on the scaling factor of the Y axis in the projection matrix
     */
    mViewProj.proj[1][1] *= -1;
}

void Camera::update(float deltaTime, bool withSmoothTransition) {
    static glm::quat interpolatedQuat{};
    if (withSmoothTransition) {
        float kDelay = deltaTime / 33.3;  // camera updating for 30 fps
        if (mInterpolationK < 1.0f) {
            mInterpolationK += 0.01f * kDelay;
        } else {
            mInterpolationK = 1.0f;
        }
    } else {
        mInterpolationK = 1.0f;
    }

    interpolatedQuat = glm::mix(mStartCameraRotation, mEndCameraRotation, mInterpolationK);
    glm::vec3 fromTargetToEye = (-1.0f * interpolatedQuat) * mFromTargetToEye;

    mViewProj.view = glm::lookAt(mTarget + fromTargetToEye, mTarget, _upDir);
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
