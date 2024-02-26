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
    vertexInputInfo.vertexBindingDescriptionCount = static_cast<uint32_t>(bindingDescription.size());
    vertexInputInfo.pVertexBindingDescriptions = bindingDescription.data();
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

    // Texture Gradient
    VkDescriptorSetLayoutBinding samplerGradientLayoutBinding = samplerLayoutBinding;
    samplerGradientLayoutBinding.binding = 3;

    std::array<VkDescriptorSetLayoutBinding, 4u> inputBindings{UBOLayoutBinding, samplerLayoutBinding, depthInputLayoutBinding,
                                                               samplerGradientLayoutBinding};

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

    VkDescriptorPoolSize textureGradientPoolSize = texturePoolSize;

    VkDescriptorPoolSize depthInputPoolSize = uboPoolSize;
    depthInputPoolSize.type = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;

    std::array<VkDescriptorPoolSize, 4u> poolSize{uboPoolSize, texturePoolSize, depthInputPoolSize, textureGradientPoolSize};

    VkDescriptorPoolCreateInfo inputPoolCreateInfo = {};
    inputPoolCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    inputPoolCreateInfo.maxSets = m_vkState._swapChain.images.size() * m_maxObjectsCount;
    inputPoolCreateInfo.poolSizeCount = poolSize.size();
    inputPoolCreateInfo.pPoolSizes = poolSize.data();

    if (vkCreateDescriptorPool(m_vkState._core.getDevice(), &inputPoolCreateInfo, nullptr, &m_descriptorPool) != VK_SUCCESS) {
        Utils::printLog(ERROR_PARAM, "failed to create descriptor pool for second pass!");
    }
    m_curMaterialId = 0u;
}

uint32_t PipelineCreatorParticle::createDescriptor(std::weak_ptr<TextureFactory::Texture> particleTexture,
                                                   VkSampler particleSampler,
                                                   std::weak_ptr<TextureFactory::Texture> gradientTexture,
                                                   VkSampler gradientSampler) {
    assert(m_vkState._core.getDevice());
    assert(m_descriptorSetLayout);
    auto sharedPtrTexture = particleTexture.lock();
    auto sharedPtrTextureGradient = gradientTexture.lock();
    assert(sharedPtrTexture && sharedPtrTextureGradient);

    ++m_curMaterialId;

    std::vector<VkDescriptorSetLayout> layouts(VulkanState::MAX_FRAMES_IN_FLIGHT, *m_descriptorSetLayout.get());
    // Input Attachment Descriptor Set Allocation Info
    VkDescriptorSetAllocateInfo setAllocInfo = {};
    setAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    setAllocInfo.descriptorPool = m_descriptorPool;
    setAllocInfo.descriptorSetCount = VulkanState::MAX_FRAMES_IN_FLIGHT;
    setAllocInfo.pSetLayouts = layouts.data();

    Material material;
    material.samplerParticle = particleSampler;
    material.textureParticle = particleTexture;
    material.textureGradient = gradientTexture;
    material.samplerGradient = gradientSampler;
    material.descriptorSetLayout = *m_descriptorSetLayout.get();

    // Allocate Descriptor Sets
    auto status = vkAllocateDescriptorSets(m_vkState._core.getDevice(), &setAllocInfo, material.descriptorSets.data());
    if (status != VK_SUCCESS) {
        Utils::printLog(ERROR_PARAM, "failed to allocate descriptor sets! ", status);
        return -1;
    }

    m_descriptorSets.try_emplace(m_curMaterialId, material);

    // Update each descriptor set with input attachment
    for (uint32_t i = 0u; i < VulkanState::MAX_FRAMES_IN_FLIGHT; ++i) {
        // UBO DESCRIPTOR
        VkDescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = m_vkState._ubo.buffers[i];
        bufferInfo.offset = 0;
        bufferInfo.range = sizeof(VulkanState::ViewProj);

        VkWriteDescriptorSet uboDescriptorWrite{};
        uboDescriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        uboDescriptorWrite.dstSet = material.descriptorSets[i];
        uboDescriptorWrite.dstBinding = 0;
        uboDescriptorWrite.dstArrayElement = 0;
        uboDescriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        uboDescriptorWrite.descriptorCount = 1;
        uboDescriptorWrite.pBufferInfo = &bufferInfo;

        // Texture
        VkDescriptorImageInfo imageInfo{};
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageInfo.imageView = sharedPtrTexture->m_textureImageView;
        imageInfo.sampler = particleSampler;

        VkWriteDescriptorSet textureSetWrite = {};
        textureSetWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        textureSetWrite.dstSet = material.descriptorSets[i];
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
        depthWrite.dstSet = material.descriptorSets[i];
        depthWrite.dstBinding = 2;
        depthWrite.dstArrayElement = 0;
        depthWrite.descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
        depthWrite.descriptorCount = 1;
        depthWrite.pImageInfo = &depthAttachmentInfo;

        // Texture Gradient
        VkDescriptorImageInfo imageGradientInfo = imageInfo;
        imageGradientInfo.imageView = sharedPtrTextureGradient->m_textureImageView;
        imageGradientInfo.sampler = gradientSampler;

        VkWriteDescriptorSet textureGradientSetWrite = textureSetWrite;
        textureSetWrite.dstBinding = 3;
        textureSetWrite.pImageInfo = &imageInfo;

        std::array<VkWriteDescriptorSet, 4u> descriptorSets{uboDescriptorWrite, textureSetWrite, depthWrite,
                                                            textureGradientSetWrite};

        // Update descriptor sets
        vkUpdateDescriptorSets(m_vkState._core.getDevice(), descriptorSets.size(), descriptorSets.data(), 0, nullptr);
    }

    return m_curMaterialId;
}

const VkDescriptorSet* PipelineCreatorParticle::getDescriptorSet(uint32_t descriptorSetsIndex, uint32_t materialId) const {
    assert(m_descriptorSets.find(materialId) != m_descriptorSets.cend());
    assert(m_descriptorSets.at(materialId).descriptorSets.size() > descriptorSetsIndex);
    return &m_descriptorSets.at(materialId).descriptorSets.at(descriptorSetsIndex);
}

void PipelineCreatorParticle::recreateDescriptors() {
    // if the counter is bigger than 0 -> no need to create descriptorSets twice
    if (m_curMaterialId > 0u) {
        return;
    }
    auto descriptorSets(std::move(m_descriptorSets));
    m_descriptorSets.clear();
    for (auto& material : descriptorSets) {
        createDescriptor(material.second.textureParticle, material.second.samplerParticle, material.second.textureGradient,
                         material.second.samplerGradient);
    }
}
