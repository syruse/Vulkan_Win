#pragma once

#include "I3DModel.h"
#include "PipelineCreatorShadowMap.h"

class PipelineCreatorFootprint : public PipelineCreatorShadowMap {
public:

    PipelineCreatorFootprint(const VulkanState& vkState, VkRenderPass& renderPass, std::string_view vertShader,
                             std::string_view fragShader, uint32_t subpass = 0u,
                             VkPushConstantRange pushConstantRange = {0u, 0u, 0u})
        : PipelineCreatorShadowMap(vkState, renderPass, vertShader, fragShader, subpass, pushConstantRange) {
    }

    void createDescriptorPool() override;
    void recreateDescriptors() override;

    const VkDescriptorSet* getDescriptorSet(uint32_t descriptorSetsIndex, uint32_t materialId = 0u) const override;

    virtual uint32_t createDescriptor(std::weak_ptr<TextureFactory::Texture>, VkSampler);

    /// it must be called for every 3d model instancing to know how big pool needed
    void increaseUsageCounter() {
        ++m_maxObjectsCount;
    }

private:
    void createPipeline() override;
    void createDescriptorSetLayout() override;

private:
    uint32_t m_maxObjectsCount{0u};
    uint32_t m_curMaterialId{0u};
    std::unordered_map<uint32_t, I3DModel::Material> m_descriptorSets{};
};
