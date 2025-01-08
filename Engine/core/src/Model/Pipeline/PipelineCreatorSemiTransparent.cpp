#include "PipelineCreatorSemiTransparent.h"
#include <assert.h>
#include "Utils.h"

void PipelineCreatorSemiTransparent::createPipeline() {
    assert(m_descriptorSetLayout);
    assert(m_renderPass);
    assert(m_vkState._core.getDevice());

    auto& blendInfo = Pipeliner::getInstance().getColorBlendInfo();
    blendInfo.attachmentCount = 1;  // Color output attachment only

    auto& raster = Pipeliner::getInstance().getRasterizationInfo();
    raster.cullMode = VK_CULL_MODE_NONE;

    // DEPTH TEST IS APPLIED INSIDE SHADER
    auto& depthStencil = Pipeliner::getInstance().getDepthStencilInfo();
    depthStencil.depthTestEnable = VK_FALSE;
    depthStencil.depthWriteEnable = VK_FALSE;

    m_pipeline = Pipeliner::getInstance().createPipeLine(m_vertShader, m_fragShader, m_vkState._width, m_vkState._height,
                                                         *m_descriptorSetLayout.get(), m_renderPass, m_vkState._core.getDevice(),
                                                         m_subpassAmount, m_pushConstantRange);
    assert(m_pipeline);
}

void PipelineCreatorSemiTransparent::createDescriptorSetLayout() {
    // dynamic UBO Binding Info
    VkDescriptorSetLayoutBinding dynamicUBOLayoutBinding = {};
    dynamicUBOLayoutBinding.binding = 0;
    dynamicUBOLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    dynamicUBOLayoutBinding.descriptorCount = 1;
    dynamicUBOLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    dynamicUBOLayoutBinding.pImmutableSamplers = nullptr;

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

    std::array<VkDescriptorSetLayoutBinding, 3u> inputBindings{dynamicUBOLayoutBinding, samplerLayoutBinding,
                                                               depthInputLayoutBinding};

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

void PipelineCreatorSemiTransparent::createDescriptorPool() {
    assert(m_descriptorPool == nullptr);  // avoid multiple alocation of the same pool
    uint32_t descriptorCount =
        VulkanState::MAX_FRAMES_IN_FLIGHT * m_maxObjectsCount *
        10;  // Maximum number of Descriptor Sets that can be created from pool (it's because 3d model may consist of subobjects)

    VkDescriptorPoolSize uboPoolSize = {};
    uboPoolSize.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    uboPoolSize.descriptorCount = descriptorCount;

    VkDescriptorPoolSize texturePoolSize = uboPoolSize;
    texturePoolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;

    VkDescriptorPoolSize depthInputPoolSize = uboPoolSize;
    depthInputPoolSize.type = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;

    std::array<VkDescriptorPoolSize, 3u> poolSize{uboPoolSize, texturePoolSize, depthInputPoolSize};

    VkDescriptorPoolCreateInfo inputPoolCreateInfo = {};
    inputPoolCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    inputPoolCreateInfo.maxSets = descriptorCount;
    inputPoolCreateInfo.poolSizeCount = poolSize.size();
    inputPoolCreateInfo.pPoolSizes = poolSize.data();

    if (vkCreateDescriptorPool(m_vkState._core.getDevice(), &inputPoolCreateInfo, nullptr, &m_descriptorPool) != VK_SUCCESS) {
        Utils::printLog(ERROR_PARAM, "failed to create descriptor pool for second pass!");
    }
    m_curMaterialId = 0u;
}

uint32_t PipelineCreatorSemiTransparent::createDescriptor(std::weak_ptr<TextureFactory::Texture> texture, VkSampler sampler) {
    assert(m_vkState._core.getDevice());

    assert(m_descriptorSetLayout);
    auto sharedPtrTexture = texture.lock();
    assert(sharedPtrTexture);

    m_curMaterialId++;

    std::vector<VkDescriptorSetLayout> layouts(VulkanState::MAX_FRAMES_IN_FLIGHT, *m_descriptorSetLayout.get());
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = m_descriptorPool;
    allocInfo.descriptorSetCount = VulkanState::MAX_FRAMES_IN_FLIGHT;
    allocInfo.pSetLayouts = layouts.data();

    I3DModel::Material material;
    material.sampler = sampler;
    material.texture = texture;
    material.descriptorSetLayout = *m_descriptorSetLayout.get();

    auto status = vkAllocateDescriptorSets(m_vkState._core.getDevice(), &allocInfo, material.descriptorSets.data());
    if (status != VK_SUCCESS) {
        Utils::printLog(ERROR_PARAM, "failed to allocate descriptor sets! ", status);
    } else {
        m_descriptorSets.try_emplace(m_curMaterialId, material);
    }

    // connect the descriptors with buffer when binding
    for (uint32_t i = 0u; i < VulkanState::MAX_FRAMES_IN_FLIGHT; ++i) {
        // Dynamic UBO DESCRIPTOR
        VkDescriptorBufferInfo DUBOInfo = {};
        DUBOInfo.buffer = m_vkState._dynamicUbo.buffers[i];
        DUBOInfo.offset = 0;
        DUBOInfo.range = m_vkState._modelUniformAlignment;

        VkWriteDescriptorSet dynamicUBOSetWrite = {};
        dynamicUBOSetWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        dynamicUBOSetWrite.dstSet = material.descriptorSets[i];
        dynamicUBOSetWrite.dstBinding = 0;
        dynamicUBOSetWrite.dstArrayElement = 0;
        dynamicUBOSetWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
        dynamicUBOSetWrite.descriptorCount = 1;
        dynamicUBOSetWrite.pBufferInfo = &DUBOInfo;

        // Texture
        VkDescriptorImageInfo imageInfo{};
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageInfo.imageView = sharedPtrTexture->m_textureImageView;
        imageInfo.sampler = sampler;

        VkWriteDescriptorSet textureSetWrite = {};
        textureSetWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        textureSetWrite.dstSet = material.descriptorSets[i];
        textureSetWrite.dstBinding = 1;
        textureSetWrite.dstArrayElement = 0;
        textureSetWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        textureSetWrite.descriptorCount = m_texturesAmount;
        textureSetWrite.pImageInfo = &imageInfo;

        // Depth Attachment
        VkDescriptorImageInfo depthAttachmentInfo{};
        depthAttachmentInfo.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        depthAttachmentInfo.imageView = m_vkState._depthBuffer.depthImageView;
        depthAttachmentInfo.sampler = VK_NULL_HANDLE;

        VkWriteDescriptorSet depthWrite{};
        depthWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        depthWrite.dstSet = material.descriptorSets[i];
        depthWrite.dstBinding = 2;
        depthWrite.dstArrayElement = 0;
        depthWrite.descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
        depthWrite.descriptorCount = 1;
        depthWrite.pImageInfo = &depthAttachmentInfo;

        // List of Descriptor Set Writes
        std::vector<VkWriteDescriptorSet> setWrites{dynamicUBOSetWrite, textureSetWrite, depthWrite};

        // Update the descriptor sets with new buffer/binding info
        vkUpdateDescriptorSets(m_vkState._core.getDevice(), static_cast<uint32_t>(setWrites.size()), setWrites.data(), 0,
                               nullptr);
    }

    return m_curMaterialId;
}
