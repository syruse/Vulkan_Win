#pragma once

#include "PipelineCreatorBase.h"

class PipelineCreatorShadowMap : public PipelineCreatorBase {
public:
    using descriptorSets = std::array<VkDescriptorSet, VulkanState::MAX_FRAMES_IN_FLIGHT>;

    PipelineCreatorShadowMap(const VulkanState::DepthBuffer& depthBuffer, const VulkanState& vkState, VkRenderPass& renderPass,
                             std::string_view vertShader, std::string_view fragShader, uint32_t subpass = 0u,
                             VkPushConstantRange pushConstantRange = {0u, 0u, 0u})
        : PipelineCreatorBase(vkState, renderPass, vertShader, fragShader, subpass, pushConstantRange), m_depthBuffer(depthBuffer) {
    }

    void createDescriptorPool() override;
    void recreateDescriptors() override;

    const VkDescriptorSet* getDescriptorSet(uint32_t descriptorSetsIndex, uint32_t materialId = 0u) const override {
        assert(descriptorSetsIndex < m_descriptorSets.size());
        return &m_descriptorSets[descriptorSetsIndex];
    }

private:
    void createPipeline() override;
    void createDescriptorSetLayout() override;

protected:
    descriptorSets m_descriptorSets{};
    const VulkanState::DepthBuffer& m_depthBuffer;
};
