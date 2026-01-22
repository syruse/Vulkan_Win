#include "I3DModel.h"
#include "PipelineCreatorFootprint.h"
#include "PipelineCreatorTextured.h"

#include <cassert>
#include <future>

I3DModel::I3DModel(const VulkanState& vulkanState, TextureFactory& textureFactory,
                   PipelineCreatorTextured* pipelineCreatorTextured, PipelineCreatorFootprint* pipelineCreatorFootprint,
                   float vertexMagnitudeMultiplier, const std::vector<Instance>& instances) noexcept(true)
    : m_vkState(vulkanState),
      m_textureFactory(textureFactory),
      m_pipelineCreatorTextured(pipelineCreatorTextured),
      m_pipelineCreatorFootprint(pipelineCreatorFootprint),
      m_vertexMagnitudeMultiplier(vertexMagnitudeMultiplier),
      m_instances(instances) {
    assert(m_vertexMagnitudeMultiplier > 0.01f);
    // Note: we must have at least one instance to draw
    if (m_instances.empty()) {
        m_instances.push_back({glm::vec3(0.0f), 1.0f});
    }
    assert(pipelineCreatorTextured);
    pipelineCreatorTextured->increaseUsageCounter();
    // footprint is optional
    if (m_pipelineCreatorFootprint) {
        m_pipelineCreatorFootprint->increaseUsageCounter();
    }
}

void I3DModel::sortInstances(uint32_t currentImage, const glm::mat4& viewProj, float z_far, uint32_t activePoolThreads) {
    assert(currentImage < VulkanState::MAX_FRAMES_IN_FLIGHT);
    if (m_instances.size() <= 1u) {
        // nothing to update
        m_activeInstances = m_instances;
        return;
    }

    m_activeInstances.clear();

    const float biasValue = m_radius + 0.15f * z_far;  // plus shift to avoid choppy clipping of the model edges nearby the camera

    static constexpr float maxLimitVal = 1.0f + std::numeric_limits<float>::epsilon();
    static constexpr std::size_t minActivePoolThreads = 4u;

    std::vector<std::future<void>> workerThreads(max(activePoolThreads, minActivePoolThreads));
    std::vector<std::vector<Instance>> activeInstances(max(activePoolThreads, minActivePoolThreads));
    std::size_t chunkOffset{0u};
    std::size_t indexFrom{0u};
    std::size_t indexTo{0u};
    std::size_t workerThreadIndexPlusOne{0u};

    chunkOffset = m_instances.size() / workerThreads.size();
    for (std::size_t workerThreadIndex = 0u; workerThreadIndex < workerThreads.size(); ++workerThreadIndex) {
        workerThreadIndexPlusOne = workerThreadIndex + 1U;
        indexFrom = workerThreadIndex * chunkOffset;
        indexTo = workerThreadIndexPlusOne >= workerThreads.size() ? m_instances.size() : workerThreadIndexPlusOne * chunkOffset;
        workerThreads[workerThreadIndex] =
            std::async(std::launch::async, &I3DModel::filterInstances, this, indexFrom, indexTo, biasValue, std::cref(viewProj),
                       std::ref(activeInstances[workerThreadIndex]));
    }

    for (auto& thread : workerThreads) {
        thread.wait();
    }

    m_activeInstances.clear();
    for (const auto& instances : activeInstances) {
        m_activeInstances.insert(m_activeInstances.end(), instances.begin(), instances.end());
    }
}

void I3DModel::filterInstances(std::size_t indexFrom, std::size_t indexTo, float biasValue, const glm::mat4& viewProj,
                               std::vector<Instance>& activeInstances) {
    assert(indexFrom < m_instances.size() && indexTo <= m_instances.size());
    static const float maxLimitVal = 1.0f + std::numeric_limits<float>::epsilon();

    activeInstances.clear();

    // extract frustum planes from View-Projection (Gribb-Hartmann algorithm)
    glm::vec4 planes[6];
    for (int i = 0; i < 4; ++i) {
        // accessing like viewProj[3] is collumn but we need process it as rows
        planes[0][i] = viewProj[i][3] + viewProj[i][0];  // Left
        planes[1][i] = viewProj[i][3] - viewProj[i][0];  // Right
        planes[2][i] = viewProj[i][3] + viewProj[i][1];  // Bottom
        planes[3][i] = viewProj[i][3] - viewProj[i][1];  // Top
        planes[4][i] = viewProj[i][3] + viewProj[i][2];  // Near
        planes[5][i] = viewProj[i][3] - viewProj[i][2];  // Far
    }

    // Normalize the planes to calculate the distance correctly
    for (int i = 0; i < 6; ++i) {
        planes[i] /= glm::length(glm::vec3(planes[i]));  // for normalizing w is not needed
    }

    // check whether sphere (instance) is inside the frustum
    for (std::size_t i = indexFrom; i < indexTo; i++) {
        const Instance& instance = m_instances[i];
        const glm::vec3& center = instance.posShift;
        const float scaledRadius = biasValue * instance.scale;

        bool isInsideFrustum = true;
        for (int p = 0; p < 6; ++p) {
            // Distance from the center of the sphere to the plane: dot(plane.xyz, center) + plane.w
            // If the distance is less than -radius, the sphere is completely outside the plane
            if (glm::dot(glm::vec3(planes[p]), center) + planes[p].w < -scaledRadius) {
                isInsideFrustum = false;
                break;
            }
        }

        if (isInsideFrustum) {
            activeInstances.push_back(instance);
        }
    }
}
