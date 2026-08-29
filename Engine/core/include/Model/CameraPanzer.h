#pragma once

#include "Camera.h"

class CameraPanzer : public Camera {
public:
    CameraPanzer(const Perstective& perstective, const glm::vec3& eye,
                 const glm::vec3& target = glm::vec3(0.0f, 0.0f, 0.0f));

    // Base camera update + additional view-only yaw offset (Q/E).
    void update(float deltaTime, bool withSmoothTransition = true) override;
    // Keep custom view offset in sync after movement updates base camera state.
    void move(EDirection dir) override;
    // Keep cached custom view/projection in sync with base perspective reset.
    void resetPerspective(const Perstective& perstective) override;

    // Rotate only the view offset around the target, preserving movement orientation.
    void adjustViewYaw(float deltaDeg);

    // Return the tank-body transform with the independent barrel yaw applied.
    glm::mat4 barrelModelMat();

    // Return the yaw-adjusted eye position used by rendering constants.
    glm::vec3 cameraPosition() override;
    // Return view/projection with panzer-specific view yaw applied.
    const ViewProj& viewProjMat() override;

private:
    void rebuildViewWithYaw();

private:
    float mViewYawDeg{0.0f};
    ViewProj mPanzerViewProj{};
};
