#include "PipelineCreatorSemiTransparent.h"
#include <assert.h>
#include "I3DModel.h"
#include "Utils.h"

void PipelineCreatorSemiTransparent::createPipeline() {
    assert(m_descriptorSetLayout);
    assert(m_renderPass);
    assert(m_vkState._core.getDevice());

    auto& vertexInputInfo = Pipeliner::getInstance().getVertexInputInfo();
    auto& bindingDescriptions = I3DModel::Vertex::getBindingDescription();
    auto baseAttributeDescriptions = I3DModel::Vertex::getAttributeDescriptions();
    static std::array<VkVertexInputAttributeDescription, 15> attributeDescriptions{};
    attributeDescriptions.fill({});
    for (size_t i = 0; i < baseAttributeDescriptions.size(); ++i) {
        attributeDescriptions[i] = baseAttributeDescriptions[i];
    }

    attributeDescriptions[11].binding = 1;
    attributeDescriptions[11].location = 11;
    attributeDescriptions[11].format = VK_FORMAT_R16G16B16A16_SFLOAT;
    attributeDescriptions[11].offset = offsetof(Instance, prev_model_col0);

    attributeDescriptions[12].binding = 1;
    attributeDescriptions[12].location = 12;
    attributeDescriptions[12].format = VK_FORMAT_R16G16B16A16_SFLOAT;
    attributeDescriptions[12].offset = offsetof(Instance, prev_model_col1);

    attributeDescriptions[13].binding = 1;
    attributeDescriptions[13].location = 13;
    attributeDescriptions[13].format = VK_FORMAT_R16G16B16A16_SFLOAT;
    attributeDescriptions[13].offset = offsetof(Instance, prev_model_col2);

    attributeDescriptions[14].binding = 1;
    attributeDescriptions[14].location = 14;
    attributeDescriptions[14].format = VK_FORMAT_R16G16B16A16_SFLOAT;
    attributeDescriptions[14].offset = offsetof(Instance, prev_model_col3);

    vertexInputInfo.vertexBindingDescriptionCount = bindingDescriptions.size();
    vertexInputInfo.pVertexBindingDescriptions = bindingDescriptions.data();
    vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
    vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

    auto& blendInfo = Pipeliner::getInstance().getColorBlendInfo();
    blendInfo.attachmentCount = 2;  // Color + motion vectors
    auto blendAttachments = const_cast<VkPipelineColorBlendAttachmentState*>(blendInfo.pAttachments);
    blendAttachments[1] = blendAttachments[0];
    blendAttachments[1].blendEnable = VK_FALSE;
    blendAttachments[1].colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT;

    auto& raster = Pipeliner::getInstance().getRasterizationInfo();
    raster.cullMode = VK_CULL_MODE_NONE;

    auto& depthStencil = Pipeliner::getInstance().getDepthStencilInfo();
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    
    m_pipeline = Pipeliner::getInstance().createPipeLine(m_vertShader, m_fragShader, m_vkState._offscreenWidth, m_vkState._offscreenHeight,
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

    VkDescriptorSetLayoutBinding uboViewProjLayoutBinding{};
    uboViewProjLayoutBinding.binding = 2;
    uboViewProjLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uboViewProjLayoutBinding.descriptorCount = 1;
    uboViewProjLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    uboViewProjLayoutBinding.pImmutableSamplers = nullptr;

    std::array<VkDescriptorSetLayoutBinding, 3u> inputBindings{dynamicUBOLayoutBinding, samplerLayoutBinding,
                                                               uboViewProjLayoutBinding};

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

    VkDescriptorPoolSize uboViewProjPoolSize = uboPoolSize;
    uboViewProjPoolSize.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;

    std::array<VkDescriptorPoolSize, 3u> poolSize{uboPoolSize, texturePoolSize, uboViewProjPoolSize};

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
        textureSetWrite.descriptorCount = 1;
        textureSetWrite.pImageInfo = &imageInfo;

        VkDescriptorBufferInfo uboViewProjBufferInfo{};
        uboViewProjBufferInfo.buffer = m_vkState._ubo.buffers[i];
        uboViewProjBufferInfo.offset = 0;
        uboViewProjBufferInfo.range = sizeof(VulkanState::ViewProj);

        VkWriteDescriptorSet uboViewProjSetWrite{};
        uboViewProjSetWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        uboViewProjSetWrite.dstSet = material.descriptorSets[i];
        uboViewProjSetWrite.dstBinding = 2;
        uboViewProjSetWrite.dstArrayElement = 0;
        uboViewProjSetWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        uboViewProjSetWrite.descriptorCount = 1;
        uboViewProjSetWrite.pBufferInfo = &uboViewProjBufferInfo;

        // List of Descriptor Set Writes
        std::vector<VkWriteDescriptorSet> setWrites{dynamicUBOSetWrite, textureSetWrite, uboViewProjSetWrite};

        // Update the descriptor sets with new buffer/binding info
        vkUpdateDescriptorSets(m_vkState._core.getDevice(), static_cast<uint32_t>(setWrites.size()), setWrites.data(), 0,
                               nullptr);
    }

    return m_curMaterialId;
}
