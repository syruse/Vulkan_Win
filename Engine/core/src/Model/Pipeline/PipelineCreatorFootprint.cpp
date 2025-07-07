#include "PipelineCreatorFootprint.h"
#include <assert.h>
#include "I3DModel.h"
#include "Utils.h"

void PipelineCreatorFootprint::createPipeline() {
    assert(m_descriptorSetLayout);
    assert(m_renderPass);
    assert(m_vkState._core.getDevice());

    auto& vertexInputInfo = Pipeliner::getInstance().getVertexInputInfo();
    auto& bindingDescriptions = I3DModel::Vertex::getBindingDescription();

    std::array<VkVertexInputAttributeDescription, 2u> attributeDescriptions{};
    attributeDescriptions[0].binding = 0;
    attributeDescriptions[0].location = 0;
    attributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributeDescriptions[0].offset = offsetof(I3DModel::Vertex, I3DModel::Vertex::pos);

    attributeDescriptions[1].binding = 0;
    attributeDescriptions[1].location = 2;
    attributeDescriptions[1].format = VK_FORMAT_R32G32_SFLOAT;
    attributeDescriptions[1].offset = offsetof(I3DModel::Vertex, I3DModel::Vertex::texCoord);

    vertexInputInfo.vertexBindingDescriptionCount = bindingDescriptions.size();
    vertexInputInfo.pVertexBindingDescriptions = bindingDescriptions.data();
    vertexInputInfo.vertexAttributeDescriptionCount = attributeDescriptions.size();
    vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

    // avoiding Peter Pan effect, invisible faces generate proper shadows
    // draw both faces for plane line objects with single face
    auto& rasterInfo = Pipeliner::getInstance().getRasterizationInfo();
    rasterInfo.cullMode = VK_CULL_MODE_NONE;
    rasterInfo.depthClampEnable =
        VK_TRUE;  // fragments that are beyond the near and far planes are clamped to them as opposed to discarding them

    m_pipeline = Pipeliner::getInstance().createPipeLine(m_vertShader, m_fragShader, m_depthBuffer.width, m_depthBuffer.height,
                                                         *m_descriptorSetLayout.get(), m_renderPass, m_vkState._core.getDevice(),
                                                         m_subpassAmount, m_pushConstantRange);
    assert(m_pipeline);
}

void PipelineCreatorFootprint::createDescriptorSetLayout() {
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

    // Texture
    VkDescriptorSetLayoutBinding samplerLayoutBinding{};
    samplerLayoutBinding.binding = 2;
    samplerLayoutBinding.descriptorCount = 1;
    samplerLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    samplerLayoutBinding.pImmutableSamplers = nullptr;
    samplerLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    std::array<VkDescriptorSetLayoutBinding, 3> inputBindings{UBOLayoutBinding, DUBOLayoutBinding, samplerLayoutBinding};

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

void PipelineCreatorFootprint::createDescriptorPool() {
    assert(m_descriptorPool == nullptr);  // avoid multiple alocation of the same pool
    // Type of descriptors + how many Descriptors needed to be allocated in pool
    uint32_t descriptorCount = VulkanState::MAX_FRAMES_IN_FLIGHT * m_maxObjectsCount *
        4;  // Maximum number of Descriptor Sets that can be created from pool (it's because 3d model may consist of 4 wheels at most)

    VkDescriptorPoolSize uboPoolSize{};
    uboPoolSize.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uboPoolSize.descriptorCount = descriptorCount;

    VkDescriptorPoolSize dUboPoolSize{};
    dUboPoolSize.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    dUboPoolSize.descriptorCount = descriptorCount;

    VkDescriptorPoolSize texturePoolSize = uboPoolSize;
    texturePoolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    texturePoolSize.descriptorCount = descriptorCount;

    std::array<VkDescriptorPoolSize, 3> poolSize{uboPoolSize, dUboPoolSize, texturePoolSize};

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

uint32_t PipelineCreatorFootprint::createDescriptor(std::weak_ptr<TextureFactory::Texture> texture, VkSampler sampler) {
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
        // UBO ViewProj DESCRIPTOR
        VkDescriptorBufferInfo UBOBufferInfo{};
        UBOBufferInfo.buffer = m_vkState._ubo.buffers[i];
        UBOBufferInfo.offset = 0;
        UBOBufferInfo.range = sizeof(VulkanState::ViewProj);

        VkWriteDescriptorSet uboDescriptorWrite{};
        uboDescriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        uboDescriptorWrite.dstSet = material.descriptorSets[i];
        uboDescriptorWrite.dstBinding = 0;
        uboDescriptorWrite.dstArrayElement = 0;
        uboDescriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        uboDescriptorWrite.descriptorCount = 1;
        uboDescriptorWrite.pBufferInfo = &UBOBufferInfo;

        // Dynamic UBO DESCRIPTOR
        VkDescriptorBufferInfo DUBOInfo = {};
        DUBOInfo.buffer = m_vkState._dynamicUbo.buffers[i];
        DUBOInfo.offset = 0;
        DUBOInfo.range = m_vkState._modelUniformAlignment;

        VkWriteDescriptorSet dynamicUBOSetWrite = {};
        dynamicUBOSetWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        dynamicUBOSetWrite.dstSet = material.descriptorSets[i];
        dynamicUBOSetWrite.dstBinding = 1;
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
        textureSetWrite.dstBinding = 2;
        textureSetWrite.dstArrayElement = 0;
        textureSetWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        textureSetWrite.descriptorCount = 1;
        textureSetWrite.pImageInfo = &imageInfo;

        // List of Descriptor Set Writes
        std::array<VkWriteDescriptorSet, 3u> setWrites{dynamicUBOSetWrite, textureSetWrite, uboDescriptorWrite};

        // Update the descriptor sets with new buffer/binding info
        vkUpdateDescriptorSets(m_vkState._core.getDevice(), static_cast<uint32_t>(setWrites.size()), setWrites.data(), 0,
                               nullptr);
    }

    return m_curMaterialId;
}

const VkDescriptorSet* PipelineCreatorFootprint::getDescriptorSet(uint32_t descriptorSetsIndex, uint32_t materialId) const {
    assert(m_descriptorSets.find(materialId) != m_descriptorSets.cend());
    assert(m_descriptorSets.at(materialId).descriptorSets.size() > descriptorSetsIndex);
    return &m_descriptorSets.at(materialId).descriptorSets.at(descriptorSetsIndex);
}

void PipelineCreatorFootprint::recreateDescriptors() {
    // if the counter is bigger than 0 -> no need to create descriptorSets twice
    if (m_curMaterialId > 0u) {
        return;
    }
    std::unordered_map<uint32_t, I3DModel::Material> descriptorSets(std::move(m_descriptorSets));
    m_descriptorSets.clear();
    for (auto& material : descriptorSets) {
        createDescriptor(material.second.texture, material.second.sampler);
    }
}
