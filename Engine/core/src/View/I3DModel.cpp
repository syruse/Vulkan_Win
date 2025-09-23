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

    const float biasValue = m_radius + 0.15f * z_far;  // to avoid choppy clipping of the model edges nearby the camera

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

    glm::vec4 biasCubeValues[9] = {
        viewProj * glm::vec4(-biasValue, -biasValue, -biasValue, 1.0f),  // -Y
        viewProj * glm::vec4(biasValue, -biasValue, -biasValue, 1.0f),
        viewProj * glm::vec4(-biasValue, -biasValue, biasValue, 1.0f),
        viewProj * glm::vec4(biasValue, -biasValue, biasValue, 1.0f),
        viewProj * glm::vec4(-biasValue, biasValue, -biasValue, 1.0f),  // +Y
        viewProj * glm::vec4(biasValue, biasValue, -biasValue, 1.0f),
        viewProj * glm::vec4(-biasValue, biasValue, biasValue, 1.0f),
        viewProj * glm::vec4(biasValue, biasValue, biasValue, 1.0f),
        glm::vec4(0.0f, 0.0f, 0.0f, 0.0f)  // no bias (for center point)
    };

    for (std::size_t i = indexFrom; i < indexTo; i++) {
        const Instance& instance = m_instances[i];
        glm::vec4 clipOrig = viewProj * glm::vec4(instance.posShift, 1.0f);
        bool isTestPassed = false;
        for (const auto& bias : biasCubeValues) {
            glm::vec4 clip = clipOrig + instance.scale * bias;
            glm::vec3 ndc = glm::vec3(clip.x / clip.w, clip.y / clip.w, clip.z / clip.w);
            // z is in range [0, 1] for NDC, so we can check it against 0.0f and maxLimitVal
            if (glm::abs(ndc.x) <= maxLimitVal && glm::abs(ndc.y) <= maxLimitVal && ndc.z <= maxLimitVal &&
                ndc.z >= 0.0f - std::numeric_limits<float>::epsilon()) {
                isTestPassed = true;
                break;
            }
        }
        if (isTestPassed)
            activeInstances.push_back(instance);
    }
}
