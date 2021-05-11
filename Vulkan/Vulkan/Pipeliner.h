#pragma once
#include "vulkan/vulkan.h"
#include <string>

class Pipeliner
{
    struct PipeLine
    {
        VkShaderModule vsModule = nullptr;
        VkShaderModule fsModule = nullptr;
        VkPipeline pipeline = nullptr;
        VkPipelineLayout pipelineLayout = nullptr;
    };

public:
    Pipeliner();

    void init(std::string_view vertShader, std::string_view fragShader, 
        uint32_t width, uint32_t height, VkRenderPass renderPass, VkDevice device);

private:
    VkPipelineShaderStageCreateInfo m_shaderStageCreateInfo[2] = {};
    VkPipelineVertexInputStateCreateInfo m_vertexInputInfo = {};
    VkPipelineInputAssemblyStateCreateInfo m_pipelineIACreateInfo = {};
    VkViewport m_vp = {};
    VkRect2D m_scissor = {};
    VkPipelineViewportStateCreateInfo m_vpCreateInfo = {};
    VkPipelineRasterizationStateCreateInfo m_rastCreateInfo = {};
    VkPipelineMultisampleStateCreateInfo m_pipelineMSCreateInfo = {};
    VkPipelineColorBlendStateCreateInfo m_blendCreateInfo = {};
    VkPipelineDepthStencilStateCreateInfo m_depthStencil{};
};