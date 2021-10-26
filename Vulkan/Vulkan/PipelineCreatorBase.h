#pragma once
#include "vulkan/vulkan.h"
#include <string>
#include <memory>
#include <functional>
#include "Pipeliner.h"

class PipelineCreatorBase
{
public:

    constexpr PipelineCreatorBase(std::string_view vertShader, std::string_view fragShader, uint32_t subpass = 0u):
    m_vertShader(vertShader),
    m_fragShader(fragShader),
    m_subpassAmount(subpass)
    {
    }

    Pipeliner::pipeline_ptr init(uint32_t width, uint32_t height, 
        VkRenderPass renderPass, VkDevice device, VkPushConstantRange pushConstantRange = {0u, 0u, 0u});

    inline VkDescriptorSetLayout getDescriptorSetLayout()
    {
        return m_descriptorSetLayout;
    }

    inline const Pipeliner::pipeline_ptr& getPipeline()
    {
        return m_pipeline;
    }

private:

    virtual void createDescriptorSetLayout(VkDevice device);
    virtual void createPipeline(uint32_t width, uint32_t height, 
        VkRenderPass renderPass, VkDevice device, VkPushConstantRange pushConstantRange) = 0;

protected:

    std::string_view m_vertShader;
    std::string_view m_fragShader;
    VkDescriptorSetLayout m_descriptorSetLayout = nullptr;
    uint32_t m_subpassAmount = 0u;
    Pipeliner::pipeline_ptr m_pipeline = {nullptr, nullptr};
};