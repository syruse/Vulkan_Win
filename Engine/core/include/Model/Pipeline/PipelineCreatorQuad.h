#pragma once

#include "PipelineCreatorBase.h"

class PipelineCreatorQuad : public PipelineCreatorBase {
public:
    using descriptorSets = std::array<VkDescriptorSet, VulkanState::MAX_FRAMES_IN_FLIGHT>;

    PipelineCreatorQuad(const VulkanState& vkState, VkRenderPass& renderPass, std::string_view vertShader,
                        std::string_view fragShader, bool isDepthNeeded = false, bool isGPassNeeded = false,
                        uint32_t subpass = 0u, VkPushConstantRange pushConstantRange = {0u, 0u, 0u})
        : PipelineCreatorBase(vkState, renderPass, vertShader, fragShader, subpass, pushConstantRange),
          m_isDepthNeeded(isDepthNeeded),
          m_isGPassNeeded(isGPassNeeded) {
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

    uint32_t getInputBindingsAmount() const;

private:
    bool m_isDepthNeeded{false};
    bool m_isGPassNeeded{false};
    descriptorSets m_descriptorSets{};
};
