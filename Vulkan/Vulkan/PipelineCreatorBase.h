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

    Pipeliner::pipeline_ptr createPipeline(uint32_t width, uint32_t height, 
        VkRenderPass renderPass, VkDevice device, uint32_t subpass = 0u, VkPushConstantRange pushConstantRange = {0u, 0u, 0u});

    inline VkDescriptorSetLayout getDescriptorSetLayout()
    {
        return m_descriptorSetLayout;
    }

    inline Pipeliner::pipeline_ptr getPipeline()
    {
        return m_descriptorSetLayout;
    }

private:

    virtual void createDescriptorSetLayout(VkDevice device);
    virtual void createPipeline() = 0;

private:

    std::string_view m_vertShader;
    std::string_view m_fragShader;
    VkDescriptorSetLayout m_descriptorSetLayout = nullptr;
    uint32_t m_subpassAmount = 0u;

    ///piplines stash
    static std::unordered_map<uint16_t, Pipeliner::pipeline_ptr> _pipelines;
};