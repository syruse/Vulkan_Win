
#include "Pipeliner.h"
#include "Utils.h"
#include "I3DModel.h"

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

    m_vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    auto bindingDescription = I3DModel::Vertex::getBindingDescription();
    auto attributeDescriptions = I3DModel::Vertex::getAttributeDescriptions();
    m_vertexInputInfo.vertexBindingDescriptionCount = 1;
    m_vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
    m_vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
    m_vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

    m_pipelineIACreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    m_pipelineIACreateInfo.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

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

    m_rastCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    m_rastCreateInfo.polygonMode = VK_POLYGON_MODE_FILL;
    m_rastCreateInfo.cullMode = VK_CULL_MODE_BACK_BIT;
    m_rastCreateInfo.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    m_rastCreateInfo.lineWidth = 1.0f;

    m_pipelineMSCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    m_pipelineMSCreateInfo.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState blendAttachState = {};
    blendAttachState.colorWriteMask = 0xf;

    m_blendCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    m_blendCreateInfo.logicOp = VK_LOGIC_OP_COPY;
    m_blendCreateInfo.attachmentCount = 1;
    m_blendCreateInfo.pAttachments = &blendAttachState;

    m_depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    m_depthStencil.depthTestEnable = VK_TRUE;
    m_depthStencil.depthWriteEnable = VK_TRUE;
    m_depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
    m_depthStencil.depthBoundsTestEnable = VK_FALSE;
    m_depthStencil.minDepthBounds = 0.0f; // Optional
    m_depthStencil.maxDepthBounds = 1.0f; // Optional
    m_depthStencil.stencilTestEnable = VK_FALSE;
    m_depthStencil.front = {}; // Optional
    m_depthStencil.back = {}; // Optional
}

void Pipeliner::init(std::string_view vertShader, std::string_view fragShader, uint32_t width, uint32_t height, VkRenderPass renderPass, VkDevice device)
{
    PipeLine pipeline;
    pipeline.vsModule = Utils::VulkanCreateShaderModule(device, "vert.spv");
    assert(pipeline.vsModule);
    pipeline.fsModule = Utils::VulkanCreateShaderModule(device, "frag.spv");
    assert(pipeline.vsModule);

    m_vp.width = (float)width;
    m_vp.height = (float)height;

    m_scissor.extent.width = width;
    m_scissor.extent.height = height;

    VkPipelineLayoutCreateInfo layoutInfo = {};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 1;
    //layoutInfo.pSetLayouts = &m_descriptorSetLayout;
    layoutInfo.pushConstantRangeCount = 1;
    //layoutInfo.pPushConstantRanges = &m_pushConstantRange;

    VkResult res = vkCreatePipelineLayout(device, &layoutInfo, NULL, &pipeline.pipelineLayout);
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
    pipelineInfo.layout = pipeline.pipelineLayout;
    pipelineInfo.renderPass = renderPass;
    pipelineInfo.basePipelineIndex = -1;
    pipelineInfo.pDepthStencilState = &m_depthStencil;

    res = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, NULL, &pipeline.pipeline);
    CHECK_VULKAN_ERROR("vkCreateGraphicsPipelines error %d\n", res);
}

