#pragma once

#include "PipelineCreatorBase.h"

class PipelineCreatorQuad : public PipelineCreatorBase {
public:
    using descriptorSets = std::array<VkDescriptorSet, VulkanState::MAX_FRAMES_IN_FLIGHT>;

    PipelineCreatorQuad(const VulkanState& vkState, VkRenderPass& renderPass, std::string_view vertShader,
                        std::string_view fragShader, bool isDepthNeeded = false, bool isGPassNeeded = false,
                        uint32_t subpass = 0u, VkPushConstantRange pushConstantRange = {0u, 0u, 0u})
        : PipelineCreatorBase(vkState, renderPass, vertShader, fragShader, subpass, pushConstantRange),
          m_isBlendForBloom(false),
          m_isDepthNeeded(isDepthNeeded),
          m_isGPassNeeded(isGPassNeeded),
          m_colorBuffer(&vkState._colorBuffer) {
        assert(m_colorBuffer);
    }

    PipelineCreatorQuad(const VulkanState& vkState, VkRenderPass& renderPass, std::string_view vertShader,
                        std::string_view fragShader, VulkanState::ColorBuffer* colorBuffer, bool isBlendForBloom = false, bool isDepthNeeded = false,
                        VkPushConstantRange pushConstantRange = {0u, 0u, 0u})
        : PipelineCreatorBase(vkState, renderPass, vertShader, fragShader, 0u, pushConstantRange),
          m_isBlendForBloom(isBlendForBloom),
          m_isDepthNeeded(isDepthNeeded),
          m_isGPassNeeded(false),
          m_colorBuffer(colorBuffer){
        assert(m_colorBuffer);
    }

    void createDescriptorPool() override;
    void recreateDescriptors() override;

    const VkDescriptorSet* getDescriptorSet(uint32_t descriptorSetsIndex, uint32_t materialId = 0u) const override {
        assert(descriptorSetsIndex < m_descriptorSets.size());
        return &m_descriptorSets[descriptorSetsIndex];
    }

protected:
    void createPipeline() override;

private:
    void createDescriptorSetLayout() override;
    uint32_t getInputBindingsAmount() const;

private:
    bool m_isBlendForBloom{false};
    bool m_isDepthNeeded{false};
    bool m_isGPassNeeded{false};

protected:
    descriptorSets m_descriptorSets{};
    const VulkanState::ColorBuffer* m_colorBuffer{nullptr};
};
