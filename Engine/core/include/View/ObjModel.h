#pragma once

#include "I3DModel.h"

class ObjModel : public I3DModel {
public:
    ObjModel(const VulkanState& vulkanState, TextureFactory& textureFactory, std::string_view path,
             PipelineCreatorTextured* pipelineCreatorTextured, PipelineCreatorFootprint* pipelineCreatorFootprint,
             float vertexMagnitudeMultiplier = 1.0f, const std::vector<Instance>& instances = {}) noexcept(true)
        : I3DModel(vulkanState, textureFactory, pipelineCreatorTextured, pipelineCreatorFootprint, vertexMagnitudeMultiplier, instances),
          m_path(path) {
    }

    void init() override;
    void update(float deltaTimeMS, int animationID, bool onGPU, uint32_t currentImage = 0u,
                const glm::mat4& viewProj = glm::mat4(1.0f), float z_far = 1.0f) override;
    void draw(VkCommandBuffer cmdBuf, uint32_t descriptorSetIndex, uint32_t dynamicOffset) const override;
    void drawWithCustomPipeline(PipelineCreatorBase* pipelineCreator, VkCommandBuffer cmdBuf, uint32_t descriptorSetIndex,
                                uint32_t dynamicOffset) const override;
    void drawFootprints(VkCommandBuffer cmdBuf, uint32_t descriptorSetIndex = 0U, uint32_t dynamicOffset = 0U) const override;

    float radius() const override {
        return mRadius;
    }

private:
    void load(std::vector<Vertex>& vertices, std::vector<uint32_t>& indices);

    std::string m_path{};
    std::vector<std::vector<SubObject>> m_SubObjects{};
    std::vector<SubObject> m_Tracks{};
    float mRadius{0.0f};
    uint32_t m_activeInstances{0u};
};
