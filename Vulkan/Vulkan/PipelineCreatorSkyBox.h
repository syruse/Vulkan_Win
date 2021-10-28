#pragma once
#include "PipelineCreatorBase.h"

class PipelineCreatorSkyBox: public PipelineCreatorBase
{
public:

    constexpr PipelineCreatorSkyBox(std::string_view vertShader, std::string_view fragShader, uint32_t subpass = 0u, VkPushConstantRange pushConstantRange = {0u, 0u, 0u})
    :PipelineCreatorBase(vertShader, fragShader, subpass, pushConstantRange)
    {}


private:

    virtual void createPipeline(uint32_t width, uint32_t height, 
        VkRenderPass renderPass, VkDevice device) override;


};