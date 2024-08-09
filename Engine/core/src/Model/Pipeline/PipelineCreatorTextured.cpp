
#include "PipelineCreatorTextured.h"
#include <assert.h>

void PipelineCreatorTextured::createPipeline() {
    assert(m_descriptorSetLayout);
    assert(m_renderPass);
    assert(m_vkState._core.getDevice());

    if (!m_tessCtrlShader.empty() && !m_tessEvalShader.empty()) {
        auto& pipelineIACreateInfo = Pipeliner::getInstance().getInputAssemblyInfo();
        pipelineIACreateInfo.topology = VK_PRIMITIVE_TOPOLOGY_PATCH_LIST;

        auto& tessInfo = Pipeliner::getInstance().getTessInfo();
        tessInfo.patchControlPoints = 3;

        m_pipeline = Pipeliner::getInstance().createPipeLine(
            m_vertShader, m_fragShader, m_tessCtrlShader, m_tessEvalShader, m_vkState._width, m_vkState._height,
            *m_descriptorSetLayout.get(), m_renderPass, m_vkState._core.getDevice(), m_subpassAmount, m_pushConstantRange);

    } else {
        m_pipeline = Pipeliner::getInstance().createPipeLine(m_vertShader, m_fragShader, m_vkState._width, m_vkState._height,
                                                             *m_descriptorSetLayout.get(), m_renderPass,
                                                             m_vkState._core.getDevice(), m_subpassAmount, m_pushConstantRange);
    }

    assert(m_pipeline);
}

void PipelineCreatorTextured::createDescriptorSetLayout() {
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
    samplerLayoutBinding.descriptorCount = m_texturesAmount;
    samplerLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    samplerLayoutBinding.pImmutableSamplers = nullptr;
    samplerLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    // UBO Binding Info
    VkDescriptorSetLayoutBinding uboViewProjLayoutBinding = {};
    uboViewProjLayoutBinding.binding = 2;
    uboViewProjLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uboViewProjLayoutBinding.descriptorCount = 1;
    uboViewProjLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;
    uboViewProjLayoutBinding.pImmutableSamplers = nullptr;

    std::array<VkDescriptorSetLayoutBinding, 3> layoutBindings{dynamicUBOLayoutBinding, samplerLayoutBinding,
                                                               uboViewProjLayoutBinding};

    // Create Descriptor Set Layout with given bindings
    VkDescriptorSetLayoutCreateInfo layoutCreateInfo = {};
    layoutCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutCreateInfo.bindingCount = static_cast<uint32_t>(layoutBindings.size());
    layoutCreateInfo.pBindings = layoutBindings.data();

    m_descriptorSetLayout = std::make_unique<VkDescriptorSetLayout>();
    if (vkCreateDescriptorSetLayout(m_vkState._core.getDevice(), &layoutCreateInfo, nullptr, m_descriptorSetLayout.get()) !=
        VK_SUCCESS) {
        Utils::printLog(ERROR_PARAM, "failed to create descriptor set layout!");
    }
}

void PipelineCreatorTextured::createDescriptorPool() {
    assert(m_descriptorPool == nullptr);  // avoid multiple alocation of the same pool
    // Type of descriptors + how many Descriptors needed to be allocated in pool
    uint32_t descriptorCount =
        VulkanState::MAX_FRAMES_IN_FLIGHT * m_maxObjectsCount *
        10;  // Maximum number of Descriptor Sets that can be created from pool (it's because 3d model may consist of subobjects)
    VkDescriptorPoolSize uboPoolSize = {};
    uboPoolSize.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    uboPoolSize.descriptorCount = descriptorCount;

    VkDescriptorPoolSize texturePoolSize = uboPoolSize;
    texturePoolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    texturePoolSize.descriptorCount *= m_texturesAmount;

    VkDescriptorPoolSize uboViewProjPoolSize = uboPoolSize;
    uboPoolSize.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;

    // List of pool sizes
    std::array<VkDescriptorPoolSize, 3> descriptorPoolSizes{uboPoolSize, texturePoolSize, uboViewProjPoolSize};

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = static_cast<uint32_t>(descriptorPoolSizes.size());
    poolInfo.pPoolSizes = descriptorPoolSizes.data();
    poolInfo.maxSets = descriptorCount;

    if (vkCreateDescriptorPool(m_vkState._core.getDevice(), &poolInfo, nullptr, &m_descriptorPool) != VK_SUCCESS) {
        Utils::printLog(ERROR_PARAM, "failed to create descriptor pool!");
    }

    m_curMaterialId = 0u;
}

uint32_t PipelineCreatorTextured::createDescriptor(std::weak_ptr<TextureFactory::Texture> texture, VkSampler sampler) {
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

        // UBO ViewProj DESCRIPTOR
        VkDescriptorBufferInfo UBOBufferInfo{};
        UBOBufferInfo.buffer = m_vkState._ubo.buffers[i];
        UBOBufferInfo.offset = 0;
        UBOBufferInfo.range = sizeof(VulkanState::ViewProj);

        VkWriteDescriptorSet uboDescriptorWrite{};
        uboDescriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        uboDescriptorWrite.dstSet = material.descriptorSets[i];
        uboDescriptorWrite.dstBinding = 2;
        uboDescriptorWrite.dstArrayElement = 0;
        uboDescriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        uboDescriptorWrite.descriptorCount = 1;
        uboDescriptorWrite.pBufferInfo = &UBOBufferInfo;

        // List of Descriptor Set Writes
        std::array<VkWriteDescriptorSet, 3> setWrites{dynamicUBOSetWrite, textureSetWrite, uboDescriptorWrite};

        // Update the descriptor sets with new buffer/binding info
        vkUpdateDescriptorSets(m_vkState._core.getDevice(), static_cast<uint32_t>(setWrites.size()), setWrites.data(), 0,
                               nullptr);
    }

    return m_curMaterialId;
}

const VkDescriptorSet* PipelineCreatorTextured::getDescriptorSet(uint32_t descriptorSetsIndex, uint32_t materialId) const {
    assert(m_descriptorSets.find(materialId) != m_descriptorSets.cend());
    assert(m_descriptorSets.at(materialId).descriptorSets.size() > descriptorSetsIndex);
    return &m_descriptorSets.at(materialId).descriptorSets.at(descriptorSetsIndex);
}

void PipelineCreatorTextured::recreateDescriptors() {
    // if the counter is bigger than 0 -> no need to create descriptorSets twice
    if (m_curMaterialId > 0u) {
        return;
    }
    auto descriptorSets(std::move(m_descriptorSets));
    m_descriptorSets.clear();
    for (auto& material : descriptorSets) {
        createDescriptor(material.second.texture, material.second.sampler);
    }
}
