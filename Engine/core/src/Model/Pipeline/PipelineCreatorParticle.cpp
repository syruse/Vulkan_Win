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
    blendInfo.attachmentCount = 3;  // OIT accumulation + revealage + motion vectors
    auto blendAttachments = const_cast<VkPipelineColorBlendAttachmentState*>(blendInfo.pAttachments);
    // Accumulate weighted transparent color and weight from every particle fragment.
    blendAttachments[0].blendEnable = VK_TRUE;
    blendAttachments[0].srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
    blendAttachments[0].dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
    blendAttachments[0].srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    blendAttachments[0].dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    blendAttachments[1] = blendAttachments[0];
    blendAttachments[1].colorWriteMask = VK_COLOR_COMPONENT_R_BIT;
    blendAttachments[1].srcColorBlendFactor = VK_BLEND_FACTOR_ZERO;
    blendAttachments[1].dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blendAttachments[1].srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    blendAttachments[1].dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blendAttachments[2] = blendAttachments[0];
    // Motion vectors are written directly and must not be blended with prior values.
    blendAttachments[2].blendEnable = VK_FALSE;
    blendAttachments[2].colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT;

    auto& depthStencil = Pipeliner::getInstance().getDepthStencilInfo();
    depthStencil.depthTestEnable = VK_TRUE;
    // Test against opaque depth, but do not let transparent layers occlude one another.
    depthStencil.depthWriteEnable = VK_FALSE;

    auto& pipelineIACreateInfo = Pipeliner::getInstance().getInputAssemblyInfo();
    pipelineIACreateInfo.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;

    m_pipeline = Pipeliner::getInstance().createPipeLine(m_vertShader, m_fragShader, m_vkState._offscreenWidth, m_vkState._offscreenHeight,
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

    // Texture Gradient
    VkDescriptorSetLayoutBinding samplerGradientLayoutBinding = samplerLayoutBinding;
    samplerGradientLayoutBinding.binding = 2;

    // UBO Binding Info Particle
    VkDescriptorSetLayoutBinding UBOParticleLayoutBinding = UBOLayoutBinding;
    UBOParticleLayoutBinding.binding = 3;

    VkDescriptorSetLayoutBinding instanceBufferLayoutBinding{};
    instanceBufferLayoutBinding.binding = 4u;
    instanceBufferLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    instanceBufferLayoutBinding.descriptorCount = 1u;
    instanceBufferLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_COMPUTE_BIT;
    // instanceBufferLayoutBinding will be ignored 
    // when creating the descriptor set layout for non-GHOST_GPGPU particle modes
    std::array<VkDescriptorSetLayoutBinding, 5u> inputBindings{
        UBOLayoutBinding, samplerLayoutBinding, samplerGradientLayoutBinding,
        UBOParticleLayoutBinding, instanceBufferLayoutBinding};

    // Create a descriptor set layout for input attachments
    VkDescriptorSetLayoutCreateInfo inputLayoutCreateInfo = {};
    inputLayoutCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    inputLayoutCreateInfo.bindingCount = static_cast<uint32_t>(inputBindings.size());
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
    uint32_t descriptorCount = m_vkState._swapchainImageCount * m_maxObjectsCount;

    VkDescriptorPoolSize uboPoolSize{};
    uboPoolSize.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uboPoolSize.descriptorCount = descriptorCount;

    VkDescriptorPoolSize texturePoolSize = uboPoolSize;
    texturePoolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;

    VkDescriptorPoolSize textureGradientPoolSize = texturePoolSize;

    VkDescriptorPoolSize uboParticlePoolSize = uboPoolSize;

    VkDescriptorPoolSize instanceBufferPoolSize = uboPoolSize;
    instanceBufferPoolSize.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    std::array<VkDescriptorPoolSize, 5u> poolSize{uboPoolSize, texturePoolSize, textureGradientPoolSize,
                                                  uboParticlePoolSize, instanceBufferPoolSize};

    VkDescriptorPoolCreateInfo inputPoolCreateInfo = {};
    inputPoolCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    inputPoolCreateInfo.maxSets = descriptorCount;
    inputPoolCreateInfo.poolSizeCount = static_cast<uint32_t>(poolSize.size());
    inputPoolCreateInfo.pPoolSizes = poolSize.data();

    if (vkCreateDescriptorPool(m_vkState._core.getDevice(), &inputPoolCreateInfo, nullptr, &m_descriptorPool) != VK_SUCCESS) {
        Utils::printLog(ERROR_PARAM, "failed to create descriptor pool for second pass!");
    }
    m_curMaterialId = 0u;
}

uint32_t PipelineCreatorParticle::createDescriptor(std::weak_ptr<TextureFactory::Texture> particleTexture,
                                                   VkSampler particleSampler,
                                                   std::weak_ptr<TextureFactory::Texture> gradientTexture,
                                                   VkSampler gradientSampler, Particle::UBOParticle* uboParticle,
                                                   Particle* particle) {
    return createDescriptorWithId(particleTexture, particleSampler, gradientTexture, gradientSampler, uboParticle,
                                  particle, 0u);
}

uint32_t PipelineCreatorParticle::createDescriptorWithId(std::weak_ptr<TextureFactory::Texture> particleTexture,
                                                         VkSampler particleSampler,
                                                         std::weak_ptr<TextureFactory::Texture> gradientTexture,
                                                         VkSampler gradientSampler, Particle::UBOParticle* uboParticle,
                                                         Particle* particle,
                                                         uint32_t materialId) {
    assert(m_vkState._core.getDevice());
    assert(m_descriptorSetLayout);
    assert(uboParticle);
    assert(particle);
    auto sharedPtrTexture = particleTexture.lock();
    auto sharedPtrTextureGradient = gradientTexture.lock();
    assert(sharedPtrTexture && sharedPtrTextureGradient);

    const uint32_t resolvedMaterialId = materialId == 0u ? ++m_curMaterialId : materialId;

    std::vector<VkDescriptorSetLayout> layouts(m_vkState._swapchainImageCount, *m_descriptorSetLayout.get());
    // Input Attachment Descriptor Set Allocation Info
    VkDescriptorSetAllocateInfo setAllocInfo = {};
    setAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    setAllocInfo.descriptorPool = m_descriptorPool;
    setAllocInfo.descriptorSetCount = m_vkState._swapchainImageCount;
    setAllocInfo.pSetLayouts = layouts.data();

    Material material;
    material.samplerParticle = particleSampler;
    material.textureParticle = particleTexture;
    material.textureGradient = gradientTexture;
    material.samplerGradient = gradientSampler;
    material.uboParticle = uboParticle;
    material.particle = particle;
    material.descriptorSetLayout = *m_descriptorSetLayout.get();
    material.descriptorSets.resize(m_vkState._swapchainImageCount);

    // Allocate Descriptor Sets
    auto status = vkAllocateDescriptorSets(m_vkState._core.getDevice(), &setAllocInfo, material.descriptorSets.data());
    if (status != VK_SUCCESS) {
        Utils::printLog(ERROR_PARAM, "failed to allocate descriptor sets! ", status);
        return -1;
    }

    m_descriptorSets.insert_or_assign(resolvedMaterialId, material);

    // Update each descriptor set with input attachment
    for (uint32_t i = 0u; i < m_vkState._swapchainImageCount; ++i) {
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

        // Texture Gradient
        VkDescriptorImageInfo imageGradientInfo = imageInfo;
        imageGradientInfo.imageView = sharedPtrTextureGradient->m_textureImageView;
        imageGradientInfo.sampler = gradientSampler;

        VkWriteDescriptorSet textureGradientSetWrite = textureSetWrite;
        textureSetWrite.dstBinding = 2;
        textureSetWrite.pImageInfo = &imageGradientInfo;

        // UBO Particle DESCRIPTOR
        VkDescriptorBufferInfo bufferParticleInfo{};
        bufferParticleInfo.buffer = material.uboParticle->buffers[i];
        bufferParticleInfo.offset = 0;
        bufferParticleInfo.range = sizeof(Particle::UBOParticle::Params);

        VkWriteDescriptorSet uboParticleDescriptorWrite{};
        uboParticleDescriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        uboParticleDescriptorWrite.dstSet = material.descriptorSets[i];
        uboParticleDescriptorWrite.dstBinding = 3;
        uboParticleDescriptorWrite.dstArrayElement = 0;
        uboParticleDescriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        uboParticleDescriptorWrite.descriptorCount = 1;
        uboParticleDescriptorWrite.pBufferInfo = &bufferParticleInfo;

        std::vector<VkWriteDescriptorSet> descriptorSets{uboDescriptorWrite, textureSetWrite, textureGradientSetWrite,
                                                         uboParticleDescriptorWrite};
        descriptorSets.reserve(5u);
        VkDescriptorBufferInfo instanceBufferInfo{};
        // we need it only for GHOST_GPGPU particle mode
        if (material.particle->getParticleMode() == Particle::ParticleMode::GHOST_GPGPU) {
            instanceBufferInfo.buffer = material.particle->getInstanceBuffer(i);
            instanceBufferInfo.range = VK_WHOLE_SIZE;

            VkWriteDescriptorSet instanceBufferWrite{};
            instanceBufferWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            instanceBufferWrite.dstSet = material.descriptorSets[i];
            instanceBufferWrite.dstBinding = 4u;
            instanceBufferWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            instanceBufferWrite.descriptorCount = 1u;
            instanceBufferWrite.pBufferInfo = &instanceBufferInfo;
            descriptorSets.push_back(instanceBufferWrite);
        }

        // Update descriptor sets
        vkUpdateDescriptorSets(m_vkState._core.getDevice(), static_cast<uint32_t>(descriptorSets.size()),
                               descriptorSets.data(), 0, nullptr);
    }

    if (resolvedMaterialId > m_curMaterialId) {
        m_curMaterialId = resolvedMaterialId;
    }

    return resolvedMaterialId;
}

const VkDescriptorSet* PipelineCreatorParticle::getDescriptorSet(uint32_t descriptorSetsIndex, uint32_t materialId) const {
    assert(m_descriptorSets.find(materialId) != m_descriptorSets.cend());
    assert(m_descriptorSets.at(materialId).descriptorSets.size() > descriptorSetsIndex);
    return &m_descriptorSets.at(materialId).descriptorSets.at(descriptorSetsIndex);
}

void PipelineCreatorParticle::recreateDescriptors() {
    if (m_descriptorSets.empty()) {
        return;
    }

    auto descriptorSets(std::move(m_descriptorSets));
    m_descriptorSets.clear();
    for (auto& material : descriptorSets) {
        createDescriptorWithId(material.second.textureParticle, material.second.samplerParticle,
                               material.second.textureGradient, material.second.samplerGradient,
                               material.second.uboParticle, material.second.particle, material.first);
    }
}
