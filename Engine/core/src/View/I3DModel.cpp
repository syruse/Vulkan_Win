#include "I3DModel.h"
#include "PipelineCreatorFootprint.h"
#include "PipelineCreatorTextured.h"

#include <cassert>
#include <ranges>
#include <future>

I3DModel::I3DModel(const VulkanState& vulkanState, TextureFactory& textureFactory,
                   PipelineCreatorTextured* pipelineCreatorTextured, PipelineCreatorFootprint* pipelineCreatorFootprint,
                   float vertexMagnitudeMultiplier, const std::vector<Instance>& instances,
                   std::unique_ptr<I3DModel> lowPolyMesh) noexcept(true)
    : m_vkState(vulkanState),
      m_textureFactory(textureFactory),
      m_lowPolyMesh(std::move(lowPolyMesh)),
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

    assert(!m_lowPolyMesh ||
           (m_lowPolyMesh && !m_lowPolyMesh->m_lowPolyMesh) &&
        "LOD error: lowPolyMesh cannot have its own nested lowPolyMesh!");

    m_activeInstancesTemp.resize(ACTIVE_POOL_THREADS);
    if (m_lowPolyMesh) {
        m_activeInstancesLowPolyTemp.resize(ACTIVE_POOL_THREADS);
    } else {
        m_activeInstancesLowPolyTemp.clear();
    }
    // reserve memory
    {
        /// IN c++23 std::views::concat(activeInstances, activeInstancesLowPoly);
        std::array views = {std::views::all(m_activeInstancesTemp), std::views::all(m_activeInstancesLowPolyTemp)};
        auto size = m_instances.size();
        for (auto&& inner : views | std::views::join) {
            inner.reserve(size);
        }
    }
}

void I3DModel::sortInstances(uint32_t currentImage, const glm::mat4& viewProj, const glm::vec3& camPos, float z_far) {
    assert(currentImage < VulkanState::MAX_FRAMES_IN_FLIGHT);
    if (m_instances.size() <= 1u) {
        // nothing to update
        m_activeInstances = m_instances;
        return;
    }

    const float biasValue = m_radius + 0.15f * z_far;  // plus shift to avoid choppy clipping of the model edges nearby the camera

    std::vector<std::future<void>> workerThreads{ACTIVE_POOL_THREADS};

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
            std::async(std::launch::async, &I3DModel::filterInstances, this, indexFrom, indexTo, biasValue, 
                std::cref(viewProj), z_far,  std::cref(camPos), 
                std::ref(m_activeInstancesTemp[workerThreadIndex]),
                       m_lowPolyMesh ? std::ref(m_activeInstancesLowPolyTemp[workerThreadIndex])
                                     : std::ref(m_activeInstancesTemp[workerThreadIndex]));
    }

    for (auto& thread : workerThreads) {
        thread.wait();
    }

    m_activeInstances.clear();
    for (const auto& instances : m_activeInstancesTemp) {
        m_activeInstances.insert(m_activeInstances.end(), instances.begin(), instances.end());
    }

    if (m_lowPolyMesh) {
        m_lowPolyMesh->m_activeInstances.clear();
        for (const auto& instances : m_activeInstancesLowPolyTemp) {
            m_lowPolyMesh->m_activeInstances.insert(m_lowPolyMesh->m_activeInstances.end(), instances.begin(), instances.end());
        }
    }
}

void I3DModel::filterInstances(std::size_t indexFrom, std::size_t indexTo, float biasValue, const glm::mat4& viewProj,
                               float z_far, const glm::vec3& camPos, std::vector<Instance>& activeInstances,
                               std::vector<Instance>& activeInstancesLowPoly) {
    assert(indexFrom < m_instances.size() && indexTo <= m_instances.size());

    activeInstances.clear();
    activeInstancesLowPoly.clear();

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

    const float lodThreshold = LOD_TRESHOLD * z_far;
    const float lodThresholdSq = lodThreshold *lodThreshold; 

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
            glm::vec3 diff = instance.posShift - camPos;
            float distSq = glm::dot(diff, diff);
            // instead of sqrt we can compare squared distances since sqrt is heavy operation
            if (distSq < lodThresholdSq) {
                activeInstances.push_back(instance);
            } else if (m_lowPolyMesh) {
                activeInstancesLowPoly.push_back(instance);
            }
        }
    }
}
