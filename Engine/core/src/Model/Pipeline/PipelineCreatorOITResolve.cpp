#include "PipelineCreatorOITResolve.h"

#include "Utils.h"

void PipelineCreatorOITResolve::createDescriptorSetLayout() {
    std::array<VkDescriptorSetLayoutBinding, 2> bindings{};
    for (uint32_t i = 0u; i < bindings.size(); ++i) {
        bindings[i].binding = i;
        bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
        bindings[i].descriptorCount = 1u;
        bindings[i].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    }
    VkDescriptorSetLayoutCreateInfo createInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    createInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    createInfo.pBindings = bindings.data();
    m_descriptorSetLayout = std::make_unique<VkDescriptorSetLayout>();
    CHECK_VULKAN_ERROR("OIT resolve descriptor layout failed %d",
                       vkCreateDescriptorSetLayout(m_vkState._core.getDevice(), &createInfo, nullptr, m_descriptorSetLayout.get()));
}

void PipelineCreatorOITResolve::createDescriptorPool() {
    VkDescriptorPoolSize poolSize{VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, m_vkState._swapchainImageCount * 2u};
    VkDescriptorPoolCreateInfo createInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    createInfo.maxSets = m_vkState._swapchainImageCount;
    createInfo.poolSizeCount = 1u;
    createInfo.pPoolSizes = &poolSize;
    CHECK_VULKAN_ERROR("OIT resolve descriptor pool failed %d",
                       vkCreateDescriptorPool(m_vkState._core.getDevice(), &createInfo, nullptr, &m_descriptorPool));
}

void PipelineCreatorOITResolve::recreateDescriptors() {
    std::vector<VkDescriptorSetLayout> layouts(m_vkState._swapchainImageCount, *m_descriptorSetLayout);
    VkDescriptorSetAllocateInfo allocateInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    allocateInfo.descriptorPool = m_descriptorPool;
    allocateInfo.descriptorSetCount = m_vkState._swapchainImageCount;
    allocateInfo.pSetLayouts = layouts.data();
    m_descriptorSets.resize(m_vkState._swapchainImageCount);
    CHECK_VULKAN_ERROR("OIT resolve descriptors failed %d",
                       vkAllocateDescriptorSets(m_vkState._core.getDevice(), &allocateInfo, m_descriptorSets.data()));

    for (uint32_t i = 0u; i < m_vkState._swapchainImageCount; ++i) {
        std::array<VkDescriptorImageInfo, 2> images{};
        images[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        images[0].imageView = m_vkState._oitAccumBuffer.colorBufferImageView[i];
        images[1].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        images[1].imageView = m_vkState._oitRevealageBuffer.colorBufferImageView[i];
        std::array<VkWriteDescriptorSet, 2> writes{};
        for (uint32_t binding = 0u; binding < writes.size(); ++binding) {
            writes[binding].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[binding].dstSet = m_descriptorSets[i];
            writes[binding].dstBinding = binding;
            writes[binding].descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
            writes[binding].descriptorCount = 1u;
            writes[binding].pImageInfo = &images[binding];
        }
        vkUpdateDescriptorSets(m_vkState._core.getDevice(), static_cast<uint32_t>(writes.size()), writes.data(), 0u, nullptr);
    }
}

void PipelineCreatorOITResolve::createPipeline() {
    auto& vertexInput = Pipeliner::getInstance().getVertexInputInfo();
    vertexInput.vertexBindingDescriptionCount = 0u;
    vertexInput.vertexAttributeDescriptionCount = 0u;
    vertexInput.pVertexBindingDescriptions = nullptr;
    vertexInput.pVertexAttributeDescriptions = nullptr;
    Pipeliner::getInstance().getDepthStencilInfo().depthTestEnable = VK_FALSE;
    Pipeliner::getInstance().getDepthStencilInfo().depthWriteEnable = VK_FALSE;
    auto& blend = Pipeliner::getInstance().getColorBlendInfo();
    blend.attachmentCount = 1u;
    auto* attachment = const_cast<VkPipelineColorBlendAttachmentState*>(blend.pAttachments);
    attachment[0].blendEnable = VK_TRUE;
    attachment[0].srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    attachment[0].dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    attachment[0].srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    attachment[0].dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    Pipeliner::getInstance().getInputAssemblyInfo().topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    m_pipeline = Pipeliner::getInstance().createPipeLine(m_vertShader, m_fragShader, m_vkState._offscreenWidth,
                                                          m_vkState._offscreenHeight, *m_descriptorSetLayout, m_renderPass,
                                                          m_vkState._core.getDevice(), m_subpassAmount);
}