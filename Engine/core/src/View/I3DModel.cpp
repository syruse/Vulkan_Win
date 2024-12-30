#include "I3DModel.h"
#include "PipelineCreatorFootprint.h"
#include "PipelineCreatorTextured.h"

#include <cassert>

I3DModel::I3DModel(const VulkanState& vulkanState, TextureFactory& textureFactory,
                   PipelineCreatorTextured* pipelineCreatorTextured, PipelineCreatorFootprint* pipelineCreatorFootprint,
                   float vertexMagnitudeMultiplier) noexcept(true)
    : m_vkState(vulkanState),
      m_textureFactory(textureFactory),
      m_pipelineCreatorTextured(pipelineCreatorTextured),
      m_pipelineCreatorFootprint(pipelineCreatorFootprint),
      m_vertexMagnitudeMultiplier(vertexMagnitudeMultiplier) {
    assert(pipelineCreatorTextured);
    pipelineCreatorTextured->increaseUsageCounter();
    // footprint is optional
    if (m_pipelineCreatorFootprint) {
        m_pipelineCreatorFootprint->increaseUsageCounter();
    }
}
