#pragma once

#include "PipelineCreatorBase.h"

class PipelineCreatorOITResolve : public PipelineCreatorBase {
public:
    PipelineCreatorOITResolve(const VulkanState& vkState, VkRenderPass& renderPass, std::string_view vertShader,
                              std::string_view fragShader)
        : PipelineCreatorBase(vkState, renderPass, vertShader, fragShader, 2u) {
    }

    void createDescriptorPool() override;
    void recreateDescriptors() override;

    const VkDescriptorSet* getDescriptorSet(uint32_t descriptorSetIndex, uint32_t = 0u) const override {
        return &m_descriptorSets[descriptorSetIndex];
    }

private:
    void createDescriptorSetLayout() override;
    void createPipeline() override;

    std::vector<VkDescriptorSet> m_descriptorSets{};
};