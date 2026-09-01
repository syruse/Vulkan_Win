#pragma once

#include "I3DModel.h"

class CubeModel : public I3DModel {
public:
    // textureName: albedo texture file for cube faces
    // halfExtent: half-size of the cube in world units
    // instances: optional instancing data for perimeter/walls
    CubeModel(const VulkanState& vulkanState, TextureFactory& textureFactory, std::string_view textureName,
              PipelineCreatorTextured* pipelineCreatorTextured, float halfExtent = 1.0f,
              const std::vector<Instance>& instances = {}) noexcept(true)
        : I3DModel(vulkanState, textureFactory, pipelineCreatorTextured, nullptr, halfExtent, instances),
          m_textureName(textureName) {
    }

    // Create GPU resources and generate textured cube mesh.
    void init(bool useTransferQueue = false) override;
    // Update visible instances (culling/sorting) and upload instance data for current frame.
    void update(float deltaTimeMS, int animationID, bool onGPU, uint32_t currentImage = 0u,
                const glm::mat4& viewProj = glm::mat4(1.0f), float z_far = 1.0f,
                const glm::vec3& camPos = glm::vec3(0.0f)) override;
    // Draw with the model's default textured pipeline.
    void draw(VkCommandBuffer cmdBuf, uint32_t descriptorSetIndex, uint32_t dynamicOffset) const override;
    // Draw with an externally supplied pipeline (shadow/depth/custom passes).
    void drawWithCustomPipeline(PipelineCreatorBase* pipelineCreator, VkCommandBuffer cmdBuf, uint32_t descriptorSetIndex,
                                uint32_t dynamicOffset) const override;

private:
    // Procedurally generate a cube with per-face UVs.
    void buildMesh(std::vector<Vertex>& vertices, std::vector<uint32_t>& indices) const;
    // Upload active instances into per-swapchain host-visible instance buffer.
    void updateBuffers(uint32_t currentImage);

private:
    // Texture filename used during material initialization.
    std::string m_textureName;
    // Cached index count for vkCmdDrawIndexed.
    uint32_t m_indicesCount{0u};
    // Material descriptor id returned by TextureFactory for binding.
    uint32_t m_materialDescriptorId{0u};
};
