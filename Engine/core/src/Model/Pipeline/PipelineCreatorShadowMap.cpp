#include "PipelineCreatorShadowMap.h"
#include <assert.h>
#include "I3DModel.h"
#include "Utils.h"

void PipelineCreatorShadowMap::createPipeline() {
    assert(m_descriptorSetLayout);
    assert(m_renderPass);
    assert(m_vkState._core.getDevice());

    auto& vertexInputInfo = Pipeliner::getInstance().getVertexInputInfo();
    auto bindingDescription = I3DModel::Vertex::getBindingDescription();

    VkVertexInputAttributeDescription attributeDescription{};
    attributeDescription.binding = 0;
    attributeDescription.location = 0;
    attributeDescription.format = VK_FORMAT_R32G32B32_SFLOAT;
    attributeDescription.offset = offsetof(I3DModel::Vertex, I3DModel::Vertex::pos);

    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
    vertexInputInfo.vertexAttributeDescriptionCount = 1;
    vertexInputInfo.pVertexAttributeDescriptions = &attributeDescription;

    // avoiding Peter Pan effect, invisible faces generate proper shadows
    // draw both faces for plane line objects with single face
    auto& rasterInfo = Pipeliner::getInstance().getRasterizationInfo();
    rasterInfo.cullMode = VK_CULL_MODE_NONE;

    m_pipeline = Pipeliner::getInstance().createPipeLine(m_vertShader, m_fragShader, m_vkState._width, m_vkState._height,
                                                         *m_descriptorSetLayout.get(), m_renderPass, m_vkState._core.getDevice(),
                                                         m_subpassAmount, m_pushConstantRange);
    assert(m_pipeline);
}

void PipelineCreatorShadowMap::createDescriptorSetLayout() {
    // UBO Binding Info
    VkDescriptorSetLayoutBinding UBOLayoutBinding = {};
    UBOLayoutBinding.binding = 0;
    UBOLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    UBOLayoutBinding.descriptorCount = 1;
    UBOLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    UBOLayoutBinding.pImmutableSamplers = nullptr;

    // Dynamic UBO Binding Info
    VkDescriptorSetLayoutBinding DUBOLayoutBinding = {};
    DUBOLayoutBinding.binding = 1;
    DUBOLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    DUBOLayoutBinding.descriptorCount = 1;
    DUBOLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    DUBOLayoutBinding.pImmutableSamplers = nullptr;

    std::array<VkDescriptorSetLayoutBinding, 2> inputBindings{UBOLayoutBinding, DUBOLayoutBinding};

    // Create a descriptor set layout for input attachments
    VkDescriptorSetLayoutCreateInfo inputLayoutCreateInfo = {};
    inputLayoutCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    inputLayoutCreateInfo.bindingCount = inputBindings.size();
    inputLayoutCreateInfo.pBindings = inputBindings.data();

    // Create Descriptor Set Layout
    m_descriptorSetLayout = std::make_unique<VkDescriptorSetLayout>();
    if (vkCreateDescriptorSetLayout(m_vkState._core.getDevice(), &inputLayoutCreateInfo, nullptr, m_descriptorSetLayout.get()) !=
        VK_SUCCESS) {
        Utils::printLog(ERROR_PARAM, "failed to create descriptor set layout for second pass!");
    }
}

void PipelineCreatorShadowMap::createDescriptorPool() {
    VkDescriptorPoolSize uboPoolSize{};
    uboPoolSize.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uboPoolSize.descriptorCount = static_cast<uint32_t>(m_vkState._swapChain.images.size());

    VkDescriptorPoolSize dUboPoolSize{};
    dUboPoolSize.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    dUboPoolSize.descriptorCount = static_cast<uint32_t>(m_vkState._swapChain.images.size());

    std::array<VkDescriptorPoolSize, 2> poolSize{uboPoolSize, dUboPoolSize};

    VkDescriptorPoolCreateInfo inputPoolCreateInfo = {};
    inputPoolCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    inputPoolCreateInfo.maxSets = m_vkState._swapChain.images.size();
    inputPoolCreateInfo.poolSizeCount = poolSize.size();
    inputPoolCreateInfo.pPoolSizes = poolSize.data();

    if (vkCreateDescriptorPool(m_vkState._core.getDevice(), &inputPoolCreateInfo, nullptr, &m_descriptorPool) != VK_SUCCESS) {
        Utils::printLog(ERROR_PARAM, "failed to create descriptor pool for second pass!");
    }
}

void PipelineCreatorShadowMap::recreateDescriptors() {
    auto descriptorSetLayout = *m_descriptorSetLayout.get();
    std::array<VkDescriptorSetLayout, VulkanState::MAX_FRAMES_IN_FLIGHT> layouts{descriptorSetLayout, descriptorSetLayout,
                                                                                 descriptorSetLayout};
    // Input Attachment Descriptor Set Allocation Info
    VkDescriptorSetAllocateInfo setAllocInfo = {};
    setAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    setAllocInfo.descriptorPool = m_descriptorPool;
    setAllocInfo.descriptorSetCount = VulkanState::MAX_FRAMES_IN_FLIGHT;
    setAllocInfo.pSetLayouts = layouts.data();

    // Allocate Descriptor Sets
    VkResult result = vkAllocateDescriptorSets(m_vkState._core.getDevice(), &setAllocInfo, m_descriptorSets.data());
    CHECK_VULKAN_ERROR("Failed to allocate Input Attachment Descriptor Sets %d", result);

    // Update each descriptor set with input attachment
    for (uint32_t i = 0u; i < VulkanState::MAX_FRAMES_IN_FLIGHT; ++i) {
        // UBO DESCRIPTOR
        VkDescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = m_vkState._ubo.buffers[i];
        bufferInfo.offset = 0;
        bufferInfo.range = sizeof(VulkanState::ViewProj);

        VkWriteDescriptorSet uboDescriptorWrite{};
        uboDescriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        uboDescriptorWrite.dstSet = m_descriptorSets[i];
        uboDescriptorWrite.dstBinding = 0;
        uboDescriptorWrite.dstArrayElement = 0;
        uboDescriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        uboDescriptorWrite.descriptorCount = 1;
        uboDescriptorWrite.pBufferInfo = &bufferInfo;

        // Dynamic UBO DESCRIPTOR
        VkDescriptorBufferInfo dynamicBufferInfo{};
        dynamicBufferInfo.buffer = m_vkState._dynamicUbo.buffers[i];
        dynamicBufferInfo.offset = 0;
        dynamicBufferInfo.range = sizeof(VulkanState::Model);

        VkWriteDescriptorSet dUboDescriptorWrite{};
        dUboDescriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        dUboDescriptorWrite.dstSet = m_descriptorSets[i];
        dUboDescriptorWrite.dstBinding = 1;
        dUboDescriptorWrite.dstArrayElement = 0;
        dUboDescriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
        dUboDescriptorWrite.descriptorCount = 1;
        dUboDescriptorWrite.pBufferInfo = &dynamicBufferInfo;

        std::array<VkWriteDescriptorSet, 2> descriptorSets{uboDescriptorWrite, dUboDescriptorWrite};

        // Update descriptor sets
        vkUpdateDescriptorSets(m_vkState._core.getDevice(), descriptorSets.size(), descriptorSets.data(), 0, nullptr);
    }
}
