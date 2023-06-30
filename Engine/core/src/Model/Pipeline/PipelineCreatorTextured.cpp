
#include "PipelineCreatorTextured.h"
#include <assert.h>

void PipelineCreatorTextured::createPipeline() {
    assert(m_descriptorSetLayout);
    assert(m_renderPass);
    assert(m_vkState._core.getDevice());

    m_pipeline = Pipeliner::getInstance().createPipeLine(m_vertShader, m_fragShader, m_vkState._width, m_vkState._height,
                                                         *m_descriptorSetLayout.get(), m_renderPass, m_vkState._core.getDevice(),
                                                         m_subpassAmount, m_pushConstantRange);
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
    samplerLayoutBinding.descriptorCount = 1;
    samplerLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    samplerLayoutBinding.pImmutableSamplers = nullptr;
    samplerLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    std::vector<VkDescriptorSetLayoutBinding> layoutBindings = {dynamicUBOLayoutBinding, samplerLayoutBinding};
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
    // Type of descriptors + how many Descriptors needed to be allocated in pool

    // Dynamic UBO Pool
    VkDescriptorPoolSize dynamicUBOPoolSize = {};
    dynamicUBOPoolSize.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    dynamicUBOPoolSize.descriptorCount = static_cast<uint32_t>(m_vkState._swapChain.images.size());

    // Texture
    VkDescriptorPoolSize texturePoolSize = {};
    texturePoolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    texturePoolSize.descriptorCount = static_cast<uint32_t>(m_vkState._swapChain.images.size());

    // List of pool sizes
    std::vector<VkDescriptorPoolSize> descriptorPoolSizes{dynamicUBOPoolSize, texturePoolSize};

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = static_cast<uint32_t>(descriptorPoolSizes.size());
    poolInfo.pPoolSizes = descriptorPoolSizes.data();
    poolInfo.maxSets = static_cast<uint32_t>(VulkanState::MAX_FRAMES_IN_FLIGHT * m_maxObjectsCount *
                                             10);  // Maximum number of Descriptor Sets that can be created from pool

    if (vkCreateDescriptorPool(m_vkState._core.getDevice(), &poolInfo, nullptr, &m_descriptorPool) != VK_SUCCESS) {
        Utils::printLog(ERROR_PARAM, "failed to create descriptor pool!");
    }
}

uint32_t PipelineCreatorTextured::createDescriptor(std::weak_ptr<TextureFactory::Texture> texture, VkSampler sampler) {
    assert(m_vkState._core.getDevice());
    auto& descriptorSetData = DescriptorSetData::instance(m_vkState);

    assert(m_descriptorSetLayout);
    assert(!texture.expired());

    descriptorSetData.m_materialId++;

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
    if (status != VK_SUCCESS && texture.expired()) {
        Utils::printLog(ERROR_PARAM, "failed to allocate descriptor sets! ", status);
    } else {
        descriptorSetData.m_descriptorSets.try_emplace(descriptorSetData.m_materialId, material);
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
        imageInfo.imageView = texture.lock()->m_textureImageView;
        imageInfo.sampler = sampler;

        VkWriteDescriptorSet textureSetWrite = {};
        textureSetWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        textureSetWrite.dstSet = material.descriptorSets[i];
        textureSetWrite.dstBinding = 1;
        textureSetWrite.dstArrayElement = 0;
        textureSetWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        textureSetWrite.descriptorCount = 1;
        textureSetWrite.pImageInfo = &imageInfo;

        // List of Descriptor Set Writes
        std::vector<VkWriteDescriptorSet> setWrites = {dynamicUBOSetWrite, textureSetWrite};

        // Update the descriptor sets with new buffer/binding info
        vkUpdateDescriptorSets(m_vkState._core.getDevice(), static_cast<uint32_t>(setWrites.size()), setWrites.data(), 0,
                               nullptr);
    }

    return descriptorSetData.m_materialId;
}

const VkDescriptorSet* PipelineCreatorTextured::getDescriptorSet(uint32_t descriptorSetsIndex, uint32_t materialId) const {
    auto& descriptorSetData = DescriptorSetData::instance(m_vkState);
    return &descriptorSetData.m_descriptorSets[materialId].descriptorSets[descriptorSetsIndex];
}

void PipelineCreatorTextured::recreateDescriptors() {
    auto& descriptorSetData = DescriptorSetData::instance(m_vkState);
    descriptorSetData.m_materialId = 0u;
    auto descriptorSets(std::move(descriptorSetData.m_descriptorSets));
    for (auto& material : descriptorSets) {
        createDescriptor(material.second.texture, material.second.sampler);
    }
}
