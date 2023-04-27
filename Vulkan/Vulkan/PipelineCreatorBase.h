#pragma once

#include "Pipeliner.h"
#include "VulkanState.h"

#include <cassert>
#include <functional>
#include <memory>
#include <string>

class PipelineCreatorBase {
public:
    using descriptor_set_layout_ptr = std::unique_ptr<VkDescriptorSetLayout, std::function<void(VkDescriptorSetLayout* p)>>;

    PipelineCreatorBase(const VulkanState& vkState, std::string_view vertShader, std::string_view fragShader,
                        uint32_t subpass = 0u, VkPushConstantRange pushConstantRange = {0u, 0u, 0u})
        : m_vkState(vkState),
          m_vertShader(vertShader),
          m_fragShader(fragShader),
          m_descriptorSetLayout(),
          m_subpassAmount(subpass),
          m_pushConstantRange(pushConstantRange),
          m_pipeline() {
    }

    void recreate(VkRenderPass renderPass);

    void destructDescriptorPool();

    virtual void createDescriptorPool() = 0;

    inline const Pipeliner::pipeline_ptr& getPipeline() {
        return m_pipeline;
    }

private:
    virtual void createDescriptorSetLayout();
    virtual void createPipeline(VkRenderPass renderPass) = 0;

protected:
    const VulkanState& m_vkState;
    VkDescriptorPool m_descriptorPool{nullptr};
    std::string_view m_vertShader{};
    std::string_view m_fragShader{};
    descriptor_set_layout_ptr m_descriptorSetLayout{nullptr};
    uint32_t m_subpassAmount{0u};
    VkPushConstantRange m_pushConstantRange{0u, 0u, 0u};
    Pipeliner::pipeline_ptr m_pipeline{nullptr};
};
