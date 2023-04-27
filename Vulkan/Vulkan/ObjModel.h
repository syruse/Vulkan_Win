#pragma once

#include "I3DModel.h"

class ObjModel : public I3DModel {
public:
    ObjModel(const VulkanState& vulkanState, TextureFactory& textureFactory, std::string_view path,
             PipelineCreatorTextured* pipelineCreatorTextured, uint32_t vertexMagnitudeMultiplier = 1U) noexcept(true)
        : I3DModel(vulkanState, textureFactory, pipelineCreatorTextured, vertexMagnitudeMultiplier), m_path(path) {
    }

    virtual void init() override;

    virtual void draw(VkCommandBuffer cmdBuf, uint32_t descriptorSetIndex, uint32_t dynamicOffset) override;

private:
    void load(std::vector<Vertex>& vertices, std::vector<uint32_t>& indices);

    std::string m_path{};
    std::vector<std::vector<SubObject>> m_SubObjects{};
};
