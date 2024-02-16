#pragma once

#include "I3DModel.h"
#include "PipelineCreatorTextured.h"

class PipelineCreatorParticle : public PipelineCreatorTextured {
public:
    using descriptorSets = std::array<VkDescriptorSet, VulkanState::MAX_FRAMES_IN_FLIGHT>;

    PipelineCreatorParticle(const VulkanState& vkState, VkRenderPass& renderPass, std::string_view vertShader,
                            std::string_view fragShader, uint32_t subpass = 0u,
                            VkPushConstantRange pushConstantRange = {0u, 0u, 0u})
        : PipelineCreatorTextured(vkState, renderPass, vertShader, fragShader, 1u, subpass, pushConstantRange) {
    }

    bool createDescriptor(std::weak_ptr<TextureFactory::Texture>, VkSampler);

    void createDescriptorPool() override;
    void recreateDescriptors() override;

    const VkDescriptorSet* getDescriptorSet(uint32_t descriptorSetsIndex,
                                            [[maybe_unused]] uint32_t materialId = 0u) const override {
        assert(descriptorSetsIndex < m_material.descriptorSets.size());
        return &m_material.descriptorSets[descriptorSetsIndex];
    }

private:
    void createPipeline() override;
    void createDescriptorSetLayout() override;

private:
    I3DModel::Material m_material;
};
