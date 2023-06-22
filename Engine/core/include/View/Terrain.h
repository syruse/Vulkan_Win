#pragma once

#include <glm/glm.hpp>
#include "I3DModel.h"
#include "Pipeliner.h"
#include "TextureFactory.h"

class Terrain : public I3DModel {
public:
    Terrain(const VulkanState& vulkanState, TextureFactory& textureFactory, std::string_view textureFileName,
            PipelineCreatorTextured* pipelineCreatorTextured, uint32_t vertexMagnitudeMultiplier = 1u) noexcept(true)
        : I3DModel(vulkanState, textureFactory, pipelineCreatorTextured, vertexMagnitudeMultiplier),
          m_textureFileName(textureFileName) {
    }

    void init() override;
    void draw(VkCommandBuffer cmdBuf, uint32_t descriptorSetIndex, uint32_t dynamicOffset) const override;

private:
    std::string_view m_textureFileName{};
    std::uint32_t m_realMaterialId{0U};
};
