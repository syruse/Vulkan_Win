#pragma once
#include "vulkan/vulkan.h"
#include <string>
#include <memory>
#include <functional>

class Pipeliner
{
    struct PipeLine
    {
        VkShaderModule vsModule = nullptr;
        VkShaderModule fsModule = nullptr;
        VkPipeline pipeline = nullptr;
        VkPipelineLayout pipelineLayout = nullptr;
    };

    using pipeline_ptr = std::unique_ptr<PipeLine, void(PipeLine *p)>;

    ///private ctor
    Pipeliner();

    friend void deletePipeLine(PipeLine *p);

public:
    static Pipeliner& getInstance()
    {
        static Pipeliner pipeliner;
        return pipeliner;
    }

    std::unique_ptr<Pipeliner::PipeLine, void(*)(PipeLine *p)> getPipeLine(std::string_view vertShader, std::string_view fragShader, 
        uint32_t width, uint32_t height, VkRenderPass renderPass, VkDevice device);

private:
    VkDevice m_device = nullptr;

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