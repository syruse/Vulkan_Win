#pragma once
#include "vulkan/vulkan.h"
#include <string>
#include <memory>
#include <functional>
#include "Pipeliner.h"

class PipelineCreatorBase
{
public:

    using descriptor_set_layout_ptr = std::unique_ptr<VkDescriptorSetLayout, void(*)(VkDescriptorSetLayout* p)>;

    constexpr PipelineCreatorBase(std::string_view vertShader, std::string_view fragShader, uint32_t subpass = 0u, VkPushConstantRange pushConstantRange = {0u, 0u, 0u}):
    m_vertShader(vertShader),
    m_fragShader(fragShader),
    m_subpassAmount(subpass),
    m_pushConstantRange(pushConstantRange)
    {
    }

    Pipeliner::pipeline_ptr recreate(uint32_t width, uint32_t height, 
        VkRenderPass renderPass, VkDevice device);

    inline const descriptor_set_layout_ptr& getDescriptorSetLayout()
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
        VkRenderPass renderPass, VkDevice device) = 0;

protected:

    std::string_view m_vertShader;
    std::string_view m_fragShader;
    descriptor_set_layout_ptr m_descriptorSetLayout = {nullptr, nullptr};
    uint32_t m_subpassAmount = 0u;
    VkPushConstantRange m_pushConstantRange = {0u, 0u, 0u};
    Pipeliner::pipeline_ptr m_pipeline = {nullptr, nullptr};
};