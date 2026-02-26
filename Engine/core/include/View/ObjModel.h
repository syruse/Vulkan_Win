#pragma once

#include "I3DModel.h"

class ObjModel : public I3DModel {
public:
    ObjModel(const VulkanState& vulkanState, TextureFactory& textureFactory, std::string_view path,
             PipelineCreatorTextured* pipelineCreatorTextured, PipelineCreatorFootprint* pipelineCreatorFootprint,
             float vertexMagnitudeMultiplier = 1.0f, const std::vector<Instance>& instances = {},
             std::unique_ptr<I3DModel> lowPolyMesh = nullptr) noexcept(true)
        : I3DModel(vulkanState, textureFactory, pipelineCreatorTextured, pipelineCreatorFootprint, vertexMagnitudeMultiplier,
                   instances, std::move(lowPolyMesh)),
          m_path(path) {
    }

    void init() override;
    void update(float deltaTimeMS, int animationID, bool onGPU, uint32_t currentImage = 0u,
                const glm::mat4& viewProj = glm::mat4(1.0f), float z_far = 1.0f, const glm::vec3& camPos = glm::vec3(0.0f)) override;
    void draw(VkCommandBuffer cmdBuf, uint32_t descriptorSetIndex, uint32_t dynamicOffset) const override;
    void drawWithCustomPipeline(PipelineCreatorBase* pipelineCreator, VkCommandBuffer cmdBuf, uint32_t descriptorSetIndex,
                                uint32_t dynamicOffset) const override;
    void drawFootprints(VkCommandBuffer cmdBuf, uint32_t descriptorSetIndex = 0U, uint32_t dynamicOffset = 0U) const override;

private:
    void load(std::vector<Vertex>& vertices, std::vector<uint32_t>& indices);
    void filterInstances(std::size_t indexFrom, std::size_t indexTo, float biasValue, const glm::mat4& viewProj,
                         std::vector<Instance>& activeInstances);
    void updateBuffers(uint32_t currentImage);

    std::string m_path{};
    std::vector<std::vector<SubObject>> m_SubObjects{};
    std::vector<SubObject> m_Tracks{};
};
