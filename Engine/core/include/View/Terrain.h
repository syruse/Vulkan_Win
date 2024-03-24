#pragma once

#include <glm/glm.hpp>
#include "I3DModel.h"
#include "Pipeliner.h"
#include "TextureFactory.h"

class Terrain : public I3DModel {
public:
    Terrain(const VulkanState& vulkanState, TextureFactory& textureFactory, std::string_view noiseTextureFileName,
            std::string_view textureFileName1, std::string_view textureFileName2,
            PipelineCreatorTextured* pipelineCreatorTextured, uint32_t vertexMagnitudeMultiplier = 1u) noexcept(true)
        : I3DModel(vulkanState, textureFactory, pipelineCreatorTextured, vertexMagnitudeMultiplier),
          m_textureFileName1(textureFileName1),
          m_textureFileName2(textureFileName2),
          m_noiseTextureFileName(noiseTextureFileName) {
    }

    void init() override;
    void draw(VkCommandBuffer cmdBuf, uint32_t descriptorSetIndex, uint32_t dynamicOffset) const override;
    void drawWithCustomPipeline(PipelineCreatorBase* pipelineCreator, VkCommandBuffer cmdBuf, uint32_t descriptorSetIndex,
                                uint32_t dynamicOffset) const override;

private:
    std::string_view m_textureFileName1{};
    std::string_view m_textureFileName2{};
    std::string_view m_noiseTextureFileName{};
    std::uint32_t m_realMaterialId{0U};
};
