#include "PipelineCreatorParticle.h"
#include <assert.h>
#include "Particle.h"
#include "Utils.h"

void PipelineCreatorParticle::createPipeline() {
    assert(m_descriptorSetLayout);
    assert(m_renderPass);
    assert(m_vkState._core.getDevice());

    auto& vertexInputInfo = Pipeliner::getInstance().getVertexInputInfo();
    const auto& bindingDescription = Particle::getBindingDescription();
    const auto& attributeDescriptions = Particle::getAttributeDescription();
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
    vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
    vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

    auto& raster = Pipeliner::getInstance().getRasterizationInfo();
    raster.cullMode = VK_CULL_MODE_NONE;

    auto& blendInfo = Pipeliner::getInstance().getColorBlendInfo();
    blendInfo.attachmentCount = 1;  // Color output attachment only

    // DEPTH TEST IS APPLIED INSIDE SHADER
    auto& depthStencil = Pipeliner::getInstance().getDepthStencilInfo();
    depthStencil.depthTestEnable = VK_FALSE;
    depthStencil.depthWriteEnable = VK_FALSE;

    auto& pipelineIACreateInfo = Pipeliner::getInstance().getInputAssemblyInfo();
    pipelineIACreateInfo.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;

    m_pipeline = Pipeliner::getInstance().createPipeLine(m_vertShader, m_fragShader, m_vkState._width, m_vkState._height,
                                                         *m_descriptorSetLayout.get(), m_renderPass, m_vkState._core.getDevice(),
                                                         m_subpassAmount, m_pushConstantRange);
    assert(m_pipeline);
}

void PipelineCreatorParticle::createDescriptorSetLayout() {
    // UBO Binding Info
    VkDescriptorSetLayoutBinding UBOLayoutBinding = {};
    UBOLayoutBinding.binding = 0;
    UBOLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    UBOLayoutBinding.descriptorCount = 1;
    UBOLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    UBOLayoutBinding.pImmutableSamplers = nullptr;

    // Texture
    VkDescriptorSetLayoutBinding samplerLayoutBinding{};
    samplerLayoutBinding.binding = 1;
    samplerLayoutBinding.descriptorCount = 1;
    samplerLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    samplerLayoutBinding.pImmutableSamplers = nullptr;
    samplerLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    // Depth attachment
    VkDescriptorSetLayoutBinding depthInputLayoutBinding{};
    depthInputLayoutBinding.binding = 2;
    depthInputLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
    depthInputLayoutBinding.descriptorCount = 1;
    depthInputLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    std::array<VkDescriptorSetLayoutBinding, 3> inputBindings{UBOLayoutBinding, samplerLayoutBinding, depthInputLayoutBinding};

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

void PipelineCreatorParticle::createDescriptorPool() {
    assert(m_descriptorPool == nullptr);  // avoid multiple alocation of the same pool
    VkDescriptorPoolSize uboPoolSize{};
    uboPoolSize.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uboPoolSize.descriptorCount = static_cast<uint32_t>(m_vkState._swapChain.images.size());

    VkDescriptorPoolSize texturePoolSize = uboPoolSize;
    texturePoolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;

    VkDescriptorPoolSize depthInputPoolSize = {};
    depthInputPoolSize.type = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
    depthInputPoolSize.descriptorCount = 1;

    std::array<VkDescriptorPoolSize, 3> poolSize{uboPoolSize, texturePoolSize, depthInputPoolSize};

    VkDescriptorPoolCreateInfo inputPoolCreateInfo = {};
    inputPoolCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    inputPoolCreateInfo.maxSets = m_vkState._swapChain.images.size();
    inputPoolCreateInfo.poolSizeCount = poolSize.size();
    inputPoolCreateInfo.pPoolSizes = poolSize.data();

    if (vkCreateDescriptorPool(m_vkState._core.getDevice(), &inputPoolCreateInfo, nullptr, &m_descriptorPool) != VK_SUCCESS) {
        Utils::printLog(ERROR_PARAM, "failed to create descriptor pool for second pass!");
    }
    m_material.descriptorSets[0] = nullptr;
    m_material.descriptorSets[1] = nullptr;
    m_material.descriptorSets[2] = nullptr;
}

bool PipelineCreatorParticle::createDescriptor(std::weak_ptr<TextureFactory::Texture> texture, VkSampler sampler) {
    assert(m_vkState._core.getDevice());
    assert(m_descriptorSetLayout);
    auto sharedPtrTexture = texture.lock();
    assert(sharedPtrTexture);

    if (m_material.descriptorSets[0]) {
        Utils::printLog(INFO_PARAM, "No need to allocate DescriptorSets again!");
        return false;
    }

    std::vector<VkDescriptorSetLayout> layouts(VulkanState::MAX_FRAMES_IN_FLIGHT, *m_descriptorSetLayout.get());
    // Input Attachment Descriptor Set Allocation Info
    VkDescriptorSetAllocateInfo setAllocInfo = {};
    setAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    setAllocInfo.descriptorPool = m_descriptorPool;
    setAllocInfo.descriptorSetCount = VulkanState::MAX_FRAMES_IN_FLIGHT;
    setAllocInfo.pSetLayouts = layouts.data();

    m_material.sampler = sampler;
    m_material.texture = texture;
    m_material.descriptorSetLayout = *m_descriptorSetLayout.get();

    // Allocate Descriptor Sets
    auto status = vkAllocateDescriptorSets(m_vkState._core.getDevice(), &setAllocInfo, m_material.descriptorSets.data());
    if (status != VK_SUCCESS) {
        Utils::printLog(ERROR_PARAM, "failed to allocate descriptor sets! ", status);
        return false;
    }

    // Update each descriptor set with input attachment
    for (uint32_t i = 0u; i < VulkanState::MAX_FRAMES_IN_FLIGHT; ++i) {
        // UBO DESCRIPTOR
        VkDescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = m_vkState._ubo.buffers[i];
        bufferInfo.offset = 0;
        bufferInfo.range = sizeof(VulkanState::ViewProj);

        VkWriteDescriptorSet uboDescriptorWrite{};
        uboDescriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        uboDescriptorWrite.dstSet = m_material.descriptorSets[i];
        uboDescriptorWrite.dstBinding = 0;
        uboDescriptorWrite.dstArrayElement = 0;
        uboDescriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        uboDescriptorWrite.descriptorCount = 1;
        uboDescriptorWrite.pBufferInfo = &bufferInfo;

        // Texture
        VkDescriptorImageInfo imageInfo{};
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageInfo.imageView = sharedPtrTexture->m_textureImageView;
        imageInfo.sampler = sampler;

        VkWriteDescriptorSet textureSetWrite = {};
        textureSetWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        textureSetWrite.dstSet = m_material.descriptorSets[i];
        textureSetWrite.dstBinding = 1;
        textureSetWrite.dstArrayElement = 0;
        textureSetWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        textureSetWrite.descriptorCount = 1;
        textureSetWrite.pImageInfo = &imageInfo;

        // Depth Attachment
        VkDescriptorImageInfo depthAttachmentInfo{};
        depthAttachmentInfo.imageLayout = VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL;
        depthAttachmentInfo.imageView = m_vkState._depthBuffer.depthImageView;
        depthAttachmentInfo.sampler = VK_NULL_HANDLE;

        VkWriteDescriptorSet depthWrite{};
        depthWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        depthWrite.dstSet = m_material.descriptorSets[i];
        depthWrite.dstBinding = 2;
        depthWrite.dstArrayElement = 0;
        depthWrite.descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
        depthWrite.descriptorCount = 1;
        depthWrite.pImageInfo = &depthAttachmentInfo;

        std::array<VkWriteDescriptorSet, 3> descriptorSets{uboDescriptorWrite, textureSetWrite, depthWrite};

        // Update descriptor sets
        vkUpdateDescriptorSets(m_vkState._core.getDevice(), descriptorSets.size(), descriptorSets.data(), 0, nullptr);
    }

    return true;
}

void PipelineCreatorParticle::recreateDescriptors() {
    createDescriptor(m_material.texture, m_material.sampler);
}
