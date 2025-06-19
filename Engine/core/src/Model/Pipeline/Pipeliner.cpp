
#include "Pipeliner.h"
#include "Constants.h"
#include "I3DModel.h"
#include "Utils.h"

#include <fstream>

void deletePipeLine(Pipeliner::PipeLine* p) {
    auto device = Pipeliner::getInstance().m_device;
    Utils::printLog(INFO_PARAM, "pipeline removal");
    assert(p);
    vkDestroyPipeline(device, p->pipeline, nullptr);
    vkDestroyPipelineLayout(device, p->pipelineLayout, nullptr);
    vkDestroyShaderModule(device, p->vsModule, nullptr);
    vkDestroyShaderModule(device, p->fsModule, nullptr);
    if (p->tsCtrlModule && p->tsEvalModule) {
        vkDestroyShaderModule(device, p->tsCtrlModule, nullptr);
        vkDestroyShaderModule(device, p->tsEvalModule, nullptr);
    }
    delete p;
}

bool Pipeliner::saveCache() {
    Utils::printLog(INFO_PARAM, "saving pipeline cache: ", Constants::PIPELINE_CACHE_FILE);
    assert(m_device);
    assert(m_pipeline_cache);
    size_t cacheDataSize = 0u;
    // Determine the size of the cache data.
    VkResult result = vkGetPipelineCacheData(m_device, m_pipeline_cache, &cacheDataSize, nullptr);
    if (result == VK_SUCCESS) {
        // Allocate a temporary store for the cache data.
        std::vector<char> buffer(cacheDataSize);
        // Retrieve the actual data from the cache.
        result = vkGetPipelineCacheData(m_device, m_pipeline_cache, &cacheDataSize, buffer.data());
        if (result == VK_SUCCESS) {
            // Open the file and write the data to it.

            std::ofstream file(std::string{Constants::PIPELINE_CACHE_FILE}, std::ios::binary);
            if (!file.is_open()) {
                Utils::printLog(ERROR_PARAM, "error when opening file: ", std::string{Constants::PIPELINE_CACHE_FILE});
            }

            file.write(buffer.data(), buffer.size());
            file.close();
        }
    }

    vkDestroyPipelineCache(m_device, m_pipeline_cache, nullptr);

    return result == VK_SUCCESS ? true : false;
}

bool Pipeliner::createCache() {
    if (m_pipeline_cache) {
        Utils::printLog(INFO_PARAM, " pipeline_cache object already exists ");
        return true;
    }

    assert(m_device);
    std::vector<char> pipeline_data;

    std::ifstream file(std::string{Constants::PIPELINE_CACHE_FILE}, std::ios::ate | std::ios::binary);
    if (!file.is_open()) {
        Utils::printLog(INFO_PARAM, " pipeline cache not found ", Constants::PIPELINE_CACHE_FILE);
    } else {
        size_t cacheDataSize = (size_t)file.tellg();
        pipeline_data.reserve(cacheDataSize);
        pipeline_data.resize(cacheDataSize);

        (cacheDataSize);
        file.seekg(0);
        file.read(pipeline_data.data(), cacheDataSize);
        file.close();
    }

    /* Add initial pipeline cache data from the cached file */
    VkPipelineCacheCreateInfo create_info{VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO};
    create_info.initialDataSize = pipeline_data.size();
    create_info.pInitialData = pipeline_data.data();

    /* Create Vulkan pipeline cache */
    VkResult result = vkCreatePipelineCache(m_device, &create_info, nullptr, &m_pipeline_cache);
    CHECK_VULKAN_ERROR("vkCreatePipelineCache error %d\n", result);

    return result == VK_SUCCESS ? true : false;
}

Pipeliner::Pipeliner() {
    m_shaderStageCreateInfo[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    m_shaderStageCreateInfo[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    m_shaderStageCreateInfo[0].module = nullptr;
    m_shaderStageCreateInfo[0].pName = "main";
    m_shaderStageCreateInfo[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    m_shaderStageCreateInfo[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    m_shaderStageCreateInfo[1].module = nullptr;
    m_shaderStageCreateInfo[1].pName = "main";
    m_shaderStageCreateInfo[2].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    m_shaderStageCreateInfo[2].stage = VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT;
    m_shaderStageCreateInfo[2].module = nullptr;
    m_shaderStageCreateInfo[2].pName = "main";
    m_shaderStageCreateInfo[3].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    m_shaderStageCreateInfo[3].stage = VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;
    m_shaderStageCreateInfo[3].module = nullptr;
    m_shaderStageCreateInfo[3].pName = "main";

    _vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    static auto bindingDescriptions = I3DModel::Vertex::getBindingDescription();
    static auto attributeDescriptions = I3DModel::Vertex::getAttributeDescriptions();
    _vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
    _vertexInputInfo.vertexBindingDescriptionCount = bindingDescriptions.size();
    _vertexInputInfo.pVertexBindingDescriptions = bindingDescriptions.data();
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

    // Default alpha blending
    VkPipelineColorBlendAttachmentState blendAttachState = {};
    blendAttachState.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    blendAttachState.blendEnable = VK_TRUE;
    blendAttachState.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    blendAttachState.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blendAttachState.colorBlendOp = VK_BLEND_OP_ADD;
    blendAttachState.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    blendAttachState.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    blendAttachState.alphaBlendOp = VK_BLEND_OP_ADD;

    static std::vector<VkPipelineColorBlendAttachmentState> blendAttachments(MAX_COLOR_ATTACHMENTS, blendAttachState);

    _blendCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    _blendCreateInfo.logicOp = VK_LOGIC_OP_COPY;
    _blendCreateInfo.attachmentCount = blendAttachments.size();
    _blendCreateInfo.pAttachments = blendAttachments.data();

    _depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    _depthStencil.depthTestEnable = VK_TRUE;
    _depthStencil.depthWriteEnable = VK_TRUE;
    _depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
    _depthStencil.depthBoundsTestEnable = VK_FALSE;
    _depthStencil.minDepthBounds = 0.0f;  // Optional
    _depthStencil.maxDepthBounds = 1.0f;  // Optional
    _depthStencil.stencilTestEnable = VK_FALSE;
    _depthStencil.front = {};  // Optional
    _depthStencil.back = {};   // Optional

    _tessInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_TESSELLATION_STATE_CREATE_INFO;
    _tessInfo.pNext = nullptr;
    _tessInfo.flags = 0;
    _tessInfo.patchControlPoints = 3;

    /// set default configuration
    m_vertexInputInfo = _vertexInputInfo;
    m_pipelineIACreateInfo = _pipelineIACreateInfo;
    m_rastCreateInfo = _rastCreateInfo;
    m_pipelineMSCreateInfo = _pipelineMSCreateInfo;
    m_blendCreateInfo = _blendCreateInfo;
    m_depthStencil = _depthStencil;
    m_tessInfo = _tessInfo;
}

Pipeliner::pipeline_ptr Pipeliner::createPipeLine(std::string_view vertShader, std::string_view fragShader, uint32_t width,
                                                  uint32_t height, VkDescriptorSetLayout descriptorSetLayout,
                                                  VkRenderPass renderPass, VkDevice device, uint32_t subpass,
                                                  VkPushConstantRange pushConstantRange) {
    return createPipeLine(vertShader, fragShader, std::string_view{}, std::string_view{}, width, height, descriptorSetLayout,
                          renderPass, device, subpass, pushConstantRange);
}

Pipeliner::pipeline_ptr Pipeliner::createPipeLine(std::string_view vertShader, std::string_view fragShader,
                                                  std::string_view tessCtrlShader, std::string_view tessEvalShader,
                                                  uint32_t width, uint32_t height, VkDescriptorSetLayout descriptorSetLayout,
                                                  VkRenderPass renderPass, VkDevice device, uint32_t subpass,
                                                  VkPushConstantRange pushConstantRange) {
    m_device = device;
    assert(m_device);
    assert(descriptorSetLayout);

    if (!m_pipeline_cache) {
        createCache();
    }

    VkGraphicsPipelineCreateInfo pipelineInfo = {};

    bool isTesselationEnabled = !tessCtrlShader.empty() && !tessEvalShader.empty();

    std::unique_ptr<PipeLine, decltype(&deletePipeLine)> pipeline(new Pipeliner::PipeLine(), deletePipeLine);
    pipeline->vsModule = Utils::VulkanCreateShaderModule(device, vertShader);
    pipeline->fsModule = Utils::VulkanCreateShaderModule(device, fragShader);
    m_shaderStageCreateInfo[0].module = pipeline->vsModule;
    m_shaderStageCreateInfo[1].module = pipeline->fsModule;

    if (isTesselationEnabled) {
        pipelineInfo.pTessellationState = &m_tessInfo;
        pipeline->tsCtrlModule = Utils::VulkanCreateShaderModule(device, tessCtrlShader);
        m_shaderStageCreateInfo[2].module = pipeline->tsCtrlModule;
        pipeline->tsEvalModule = Utils::VulkanCreateShaderModule(device, tessEvalShader);
        m_shaderStageCreateInfo[3].module = pipeline->tsEvalModule;
    }

    m_vp.width = (float)width;
    m_vp.height = (float)height;

    m_scissor.extent.width = width;
    m_scissor.extent.height = height;

    VkPipelineLayoutCreateInfo layoutInfo = {};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &descriptorSetLayout;
    if (pushConstantRange.size != 0u) {
        layoutInfo.pushConstantRangeCount = 1;
        layoutInfo.pPushConstantRanges = &pushConstantRange;
    } else {
        layoutInfo.pushConstantRangeCount = 0;
        layoutInfo.pPushConstantRanges = nullptr;
    }

    VkResult res = vkCreatePipelineLayout(device, &layoutInfo, nullptr, &pipeline->pipelineLayout);
    CHECK_VULKAN_ERROR("vkCreatePipelineLayout error %d\n", res);

    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = isTesselationEnabled ? 4u : 2u;
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

    res = vkCreateGraphicsPipelines(device, m_pipeline_cache, 1, &pipelineInfo, nullptr, &pipeline->pipeline);
    CHECK_VULKAN_ERROR("vkCreateGraphicsPipelines error %d\n", res);

    /// restore default configuration
    m_vertexInputInfo = _vertexInputInfo;
    m_pipelineIACreateInfo = _pipelineIACreateInfo;
    m_rastCreateInfo = _rastCreateInfo;
    m_pipelineMSCreateInfo = _pipelineMSCreateInfo;
    m_blendCreateInfo = _blendCreateInfo;
    m_depthStencil = _depthStencil;
    m_tessInfo = _tessInfo;

    return pipeline;
}
