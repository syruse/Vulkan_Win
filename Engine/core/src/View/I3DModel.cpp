#include "I3DModel.h"
#include "PipelineCreatorTextured.h"

#include <cassert>

I3DModel::I3DModel(const VulkanState& vulkanState, TextureFactory& textureFactory,
                   PipelineCreatorTextured* pipelineCreatorTextured, uint32_t vertexMagnitudeMultiplier) noexcept(true)
    : m_vkState(vulkanState),
      m_textureFactory(textureFactory),
      m_pipelineCreatorTextured(pipelineCreatorTextured),
      m_vertexMagnitudeMultiplier(vertexMagnitudeMultiplier) {
    assert(pipelineCreatorTextured);
    pipelineCreatorTextured->increaseUsageCounter();
}
