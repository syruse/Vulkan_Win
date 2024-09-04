#pragma once

#include "I3DModel.h"

class ObjModel : public I3DModel {
public:
    ObjModel(const VulkanState& vulkanState, TextureFactory& textureFactory, std::string_view path,
             PipelineCreatorTextured* pipelineCreatorTextured, PipelineCreatorFootprint* pipelineCreatorFootprint,
             uint32_t vertexMagnitudeMultiplier = 1U) noexcept(true)
        : I3DModel(vulkanState, textureFactory, pipelineCreatorTextured, pipelineCreatorFootprint, vertexMagnitudeMultiplier),
          m_path(path) {
    }

    void init() override;
    void draw(VkCommandBuffer cmdBuf, uint32_t descriptorSetIndex, uint32_t dynamicOffset) const override;
    void drawWithCustomPipeline(PipelineCreatorBase* pipelineCreator, VkCommandBuffer cmdBuf, uint32_t descriptorSetIndex,
                                uint32_t dynamicOffset) const override;
    void drawFootprints(VkCommandBuffer cmdBuf, uint32_t descriptorSetIndex = 0U, uint32_t dynamicOffset = 0U) const override;

private:
    void load(std::vector<Vertex>& vertices, std::vector<uint32_t>& indices);

    std::string m_path{};
    std::vector<std::vector<SubObject>> m_SubObjects{};
    std::vector<SubObject> m_Tracks{};
};
