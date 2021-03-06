
#include "Pipeliner.h"
#include "Utils.h"
#include "I3DModel.h"

///persistent default configuration
VkPipelineVertexInputStateCreateInfo Pipeliner::_vertexInputInfo = {};
VkPipelineInputAssemblyStateCreateInfo Pipeliner::_pipelineIACreateInfo = {};
VkPipelineRasterizationStateCreateInfo Pipeliner::_rastCreateInfo = {};
VkPipelineMultisampleStateCreateInfo Pipeliner::_pipelineMSCreateInfo = {};
VkPipelineColorBlendStateCreateInfo Pipeliner::_blendCreateInfo = {};
VkPipelineDepthStencilStateCreateInfo Pipeliner::_depthStencil{};

void deletePipeLine(Pipeliner::PipeLine *p)
{
    auto device = Pipeliner::getInstance().m_device;
    Utils::printLog(INFO_PARAM, "pipeline removal");
    assert(p);
    vkDestroyPipeline(device, p->pipeline, nullptr);
    vkDestroyPipelineLayout(device, p->pipelineLayout, nullptr);
    vkDestroyShaderModule(device, p->vsModule, nullptr);
    vkDestroyShaderModule(device, p->fsModule, nullptr);
    delete p;
}

Pipeliner::Pipeliner()
{
    m_shaderStageCreateInfo[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    m_shaderStageCreateInfo[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    m_shaderStageCreateInfo[0].module = nullptr;
    m_shaderStageCreateInfo[0].pName = "main";
    m_shaderStageCreateInfo[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    m_shaderStageCreateInfo[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    m_shaderStageCreateInfo[1].module = nullptr;
    m_shaderStageCreateInfo[1].pName = "main";

    _vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    static auto bindingDescription = I3DModel::Vertex::getBindingDescription();
    static auto attributeDescriptions = I3DModel::Vertex::getAttributeDescriptions();
    _vertexInputInfo.vertexBindingDescriptionCount = 1;
    _vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
    _vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
    _vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

    _pipelineIACreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    _pipelineIACreateInfo.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    m_vp.x = 0.0f;
    m_vp.y = 0.0f;
    m_vp.minDepth = 0.0f;
    m_vp.maxDepth = 1.0f;

    m_scissor.offset.x = 0;
    m_scissor.offset.y = 0;

    m_vpCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    m_vpCreateInfo.viewportCount = 1;
    m_vpCreateInfo.pViewports = &m_vp;
    m_vpCreateInfo.scissorCount = 1;
    m_vpCreateInfo.pScissors = &m_scissor;

    _rastCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    _rastCreateInfo.polygonMode = VK_POLYGON_MODE_FILL;
    _rastCreateInfo.cullMode = VK_CULL_MODE_BACK_BIT;
    _rastCreateInfo.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    _rastCreateInfo.lineWidth = 1.0f;

    _pipelineMSCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    _pipelineMSCreateInfo.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    static VkPipelineColorBlendAttachmentState blendAttachState = {};
    blendAttachState.colorWriteMask = 0xf;

    _blendCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    _blendCreateInfo.logicOp = VK_LOGIC_OP_COPY;
    _blendCreateInfo.attachmentCount = 1;
    _blendCreateInfo.pAttachments = &blendAttachState;

    _depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    _depthStencil.depthTestEnable = VK_TRUE;
    _depthStencil.depthWriteEnable = VK_TRUE;
    _depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
    _depthStencil.depthBoundsTestEnable = VK_FALSE;
    _depthStencil.minDepthBounds = 0.0f; // Optional
    _depthStencil.maxDepthBounds = 1.0f; // Optional
    _depthStencil.stencilTestEnable = VK_FALSE;
    _depthStencil.front = {}; // Optional
    _depthStencil.back = {}; // Optional

    //set default configuration
    m_vertexInputInfo = _vertexInputInfo;
    m_pipelineIACreateInfo = _pipelineIACreateInfo;
    m_rastCreateInfo = _rastCreateInfo;
    m_pipelineMSCreateInfo = _pipelineMSCreateInfo;
    m_blendCreateInfo = _blendCreateInfo;
    m_depthStencil = _depthStencil;
}

Pipeliner::pipeline_ptr Pipeliner::createPipeLine(std::string_view vertShader, std::string_view fragShader, uint32_t width, uint32_t height,
                                                VkDescriptorSetLayout descriptorSetLayout,
                                                VkRenderPass renderPass, VkDevice device, uint32_t subpass, VkPushConstantRange pushConstantRange)
{
    m_device = device;
    assert(m_device);
    assert(descriptorSetLayout);
    
    std::unique_ptr<PipeLine, decltype(&deletePipeLine)> pipeline(new Pipeliner::PipeLine(), deletePipeLine);
    pipeline->vsModule = Utils::VulkanCreateShaderModule(device, vertShader);
    assert(pipeline->vsModule);
    pipeline->fsModule = Utils::VulkanCreateShaderModule(device, fragShader);
    assert(pipeline->fsModule);
    m_shaderStageCreateInfo[0].module = pipeline->vsModule;
    m_shaderStageCreateInfo[1].module = pipeline->fsModule;

    m_vp.width = (float)width;
    m_vp.height = (float)height;

    m_scissor.extent.width = width;
    m_scissor.extent.height = height;

    VkPipelineLayoutCreateInfo layoutInfo = {};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &descriptorSetLayout;
    if (pushConstantRange.size != 0u)
    {
        layoutInfo.pushConstantRangeCount = 1;
        layoutInfo.pPushConstantRanges = &pushConstantRange;
    }
    else
    {
        layoutInfo.pushConstantRangeCount = 0;
        layoutInfo.pPushConstantRanges = nullptr;
    }

    VkResult res = vkCreatePipelineLayout(device, &layoutInfo, NULL, &pipeline->pipelineLayout);
    CHECK_VULKAN_ERROR("vkCreatePipelineLayout error %d\n", res);

    VkGraphicsPipelineCreateInfo pipelineInfo = {};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = ARRAY_SIZE_IN_ELEMENTS(m_shaderStageCreateInfo);
    pipelineInfo.pStages = &m_shaderStageCreateInfo[0];
    pipelineInfo.pVertexInputState = &m_vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &m_pipelineIACreateInfo;
    pipelineInfo.pViewportState = &m_vpCreateInfo;
    pipelineInfo.pRasterizationState = &m_rastCreateInfo;
    pipelineInfo.pMultisampleState = &m_pipelineMSCreateInfo;
    pipelineInfo.pColorBlendState = &m_blendCreateInfo;
    pipelineInfo.layout = pipeline->pipelineLayout;
    pipelineInfo.renderPass = renderPass;
    pipelineInfo.basePipelineIndex = -1;
    pipelineInfo.pDepthStencilState = &m_depthStencil;
    pipelineInfo.subpass = subpass;

    res = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, NULL, &pipeline->pipeline);
    CHECK_VULKAN_ERROR("vkCreateGraphicsPipelines error %d\n", res);

    ///restore default configuration
    m_vertexInputInfo = _vertexInputInfo;
    m_pipelineIACreateInfo = _pipelineIACreateInfo;
    m_rastCreateInfo = _rastCreateInfo;
    m_pipelineMSCreateInfo = _pipelineMSCreateInfo;
    m_blendCreateInfo = _blendCreateInfo;
    m_depthStencil = _depthStencil;

    return pipeline;
}

