#include "PipelineCreatorBase.h"
#include "Utils.h"

void PipelineCreatorBase::recreate() {
    if (!m_descriptorSetLayout) {
        createDescriptorSetLayout();
    }

    /// make a reset if exists
    m_pipeline.reset();

    if (m_descriptorSetLayout) {
        auto deleter = [device = m_vkState._core.getDevice()](VkDescriptorSetLayout* p) {
            assert(device);
            Utils::printLog(INFO_PARAM, "removal triggered");
            vkDestroyDescriptorSetLayout(device, *p, nullptr);
            delete p;
        };
        m_descriptorSetLayout.get_deleter() = deleter;
    }

    createPipeline(); // Call the virtual createPipeline(Template Method pattern)
}

void PipelineCreatorBase::destroyDescriptorPool() {
    if (m_vkState._core.getDevice() && m_descriptorPool) {
        vkDestroyDescriptorPool(m_vkState._core.getDevice(), m_descriptorPool, nullptr);
        m_descriptorPool = nullptr;
    }
}

const VkSampler* PipelineCreatorBase::getOrCreateDepthSampler() {
    if (m_samplerDepthCompare) {
        return &m_samplerDepthCompare;
    }

    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;  // for pos extraction we need to use VK_FILTER_NEAREST
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.anisotropyEnable = VK_FALSE;
    samplerInfo.maxAnisotropy = 1;
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;  /// -> [0: 1]
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = 0.0f;
    samplerInfo.mipLodBias = 0.0f;
    samplerInfo.compareEnable = VK_TRUE;
    samplerInfo.compareOp = VK_COMPARE_OP_LESS_OR_EQUAL;

    if (vkCreateSampler(m_vkState._core.getDevice(), &samplerInfo, nullptr, &m_samplerDepthCompare) != VK_SUCCESS) {
        Utils::printLog(ERROR_PARAM, "failed to create texture sampler for depth sampling!");
    }

    return &m_samplerDepthCompare;
}

const VkSampler* PipelineCreatorBase::getOrCreateCommonSampler() {
    if (m_samplerCommonPostEffect) {
        return &m_samplerCommonPostEffect;
    }

    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.anisotropyEnable = VK_FALSE;
    samplerInfo.maxAnisotropy = 1;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;  /// -> [0: 1]
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = 0.0f;
    samplerInfo.mipLodBias = 0.0f;

    if (vkCreateSampler(m_vkState._core.getDevice(), &samplerInfo, nullptr, &m_samplerCommonPostEffect) != VK_SUCCESS) {
        Utils::printLog(ERROR_PARAM, "failed to create texture sampler for common post effects!");
    }

    return &m_samplerCommonPostEffect;
}
