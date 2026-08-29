#include "CameraPanzer.h"

#include <algorithm>

namespace {
glm::vec3 _upDir = glm::vec3(0.0f, 1.0f, 0.0f);
}

CameraPanzer::CameraPanzer(const Perstective& perstective, const glm::vec3& eye, const glm::vec3& target)
    : Camera(perstective, eye, target) {
    mPanzerViewProj = Camera::viewProjMat();
    rebuildViewWithYaw();
}

void CameraPanzer::update(float deltaTime, bool withSmoothTransition) {
    Camera::update(deltaTime, withSmoothTransition);
    rebuildViewWithYaw();
}

void CameraPanzer::move(EDirection dir) {
    Camera::move(dir);
    rebuildViewWithYaw();
}

void CameraPanzer::resetPerspective(const Perstective& perstective) {
    Camera::resetPerspective(perstective);
    rebuildViewWithYaw();
}

void CameraPanzer::adjustViewYaw(float deltaDeg) {
    mViewYawDeg += deltaDeg;
    mViewYawDeg = std::clamp(mViewYawDeg, -85.0f, 85.0f);
    rebuildViewWithYaw();
}

glm::mat4 CameraPanzer::barrelModelMat() {
    const glm::mat4 barrelYaw = glm::rotate(glm::mat4(1.0f), glm::radians(mViewYawDeg), _upDir);
    return Camera::targetModelMat() * barrelYaw;
}

glm::vec3 CameraPanzer::cameraPosition() {
    const glm::vec3 target = Camera::targetPos();
    const glm::vec3 baseEye = Camera::cameraPosition();
    const glm::vec3 offset = baseEye - target;

    const glm::quat viewYaw = glm::angleAxis(glm::radians(mViewYawDeg), _upDir);
    return target + (viewYaw * offset);
}

const Camera::ViewProj& CameraPanzer::viewProjMat() {
    return mPanzerViewProj;
}

void CameraPanzer::rebuildViewWithYaw() {
    const auto& base = Camera::viewProjMat();
    mPanzerViewProj.proj = base.proj;
    mPanzerViewProj.view = glm::lookAt(cameraPosition(), Camera::targetPos(), _upDir);
}
