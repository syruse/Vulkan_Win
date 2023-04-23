#pragma once

#include "PipelineCreatorBase.h"

class PipelineCreatorQuad: public PipelineCreatorBase
{
public:
    constexpr PipelineCreatorQuad(std::string_view vertShader, std::string_view fragShader, bool isDepthNeeded = false, uint32_t subpass = 0u, VkPushConstantRange pushConstantRange = {0u, 0u, 0u})
        :PipelineCreatorBase(vertShader, fragShader, subpass, pushConstantRange),
        m_isDepthNeeded(isDepthNeeded)
    {}

private:
    virtual void createPipeline(uint32_t width, uint32_t height, 
        VkRenderPass renderPass, VkDevice device) override;
        
    virtual void createDescriptorSetLayout(VkDevice device) override;

private:
    bool m_isDepthNeeded = false;
};