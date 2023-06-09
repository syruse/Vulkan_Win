#pragma once

#include "PipelineCreatorBase.h"

class PipelineCreatorQuad : public PipelineCreatorBase {
public:
    using descriptorSets = std::array<VkDescriptorSet, VulkanState::MAX_FRAMES_IN_FLIGHT>;

    PipelineCreatorQuad(const VulkanState& vkState, std::string_view vertShader, std::string_view fragShader,
                        bool isDepthNeeded = false, bool isGPassNeeded = false, uint32_t subpass = 0u,
                        VkPushConstantRange pushConstantRange = {0u, 0u, 0u})
        : PipelineCreatorBase(vkState, vertShader, fragShader, subpass, pushConstantRange),
          m_isDepthNeeded(isDepthNeeded),
          m_isGPassNeeded(isGPassNeeded) {
    }

    void createDescriptorPool() override;

    void createDescriptor();

    const VkDescriptorSet* getDescriptorSet(size_t index) {
        assert(index < m_descriptorSets.size());
        return &m_descriptorSets[index];
    }

private:
    virtual void createPipeline(VkRenderPass renderPass) override;

    virtual void createDescriptorSetLayout() override;

private:
    bool m_isDepthNeeded{false};
    bool m_isGPassNeeded{false};
    descriptorSets m_descriptorSets{};
};
