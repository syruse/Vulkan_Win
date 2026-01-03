#include "PipelineCreatorSSAO.h"
#include <assert.h>
#include <random>
#include "Utils.h"

PipelineCreatorSSAO::PipelineCreatorSSAO(const VulkanState& vkState, VkRenderPass& renderPass, std::string_view vertShader,
                                         std::string_view fragShader, uint32_t subpass, VkPushConstantRange pushConstantRange)
    : PipelineCreatorQuad(vkState, renderPass, vertShader, fragShader, false, false, subpass, pushConstantRange) {
}

PipelineCreatorSSAO::~PipelineCreatorSSAO() {
    for (size_t i = 0u; i < VulkanState::MAX_FRAMES_IN_FLIGHT; ++i) {
        vkDestroyBuffer(m_vkState._core.getDevice(), m_ubo.buffers[i], nullptr);
        vkFreeMemory(m_vkState._core.getDevice(), m_ubo.buffersMemory[i], nullptr);
    }
    vkDestroyImageView(m_vkState._core.getDevice(), m_noiseTexture.m_textureImageView, nullptr);
    vkDestroyImage(m_vkState._core.getDevice(), m_noiseTexture.m_textureImage, nullptr);
    vkFreeMemory(m_vkState._core.getDevice(), m_noiseTexture.m_textureImageMemory, nullptr);
    vkDestroySampler(m_vkState._core.getDevice(), mSampler, nullptr);
}

void PipelineCreatorSSAO::createPipeline() {
    PipelineCreatorQuad::createPipeline();

    std::uniform_real_distribution<float> randomFloats(0.0, 1.0);
    std::default_random_engine generator;

    // ssaoKernel is semisphere with vectors for sampling
    {
        for (uint32_t i = 0u; i < UBOSemiSpheraKernel::kernelSize; ++i) {
            glm::vec4 sample(randomFloats(generator) * 2.0 - 1.0, randomFloats(generator) * 2.0 - 1.0, randomFloats(generator),
                             0.0f);
            sample = glm::normalize(sample);
            sample *= randomFloats(generator);
            float scale = i / static_cast<float>(UBOSemiSpheraKernel::kernelSize);
            scale = glm::mix(0.01f, 1.0f, scale * scale);  // for positioning closer to origin (parabola graph due to scale^2)
            sample *= scale;
            m_ubo.params.samples[i] = sample;
        }

        const VkDeviceSize uboBufSize = sizeof(UBOSemiSpheraKernel::Params);
        VkBuffer stagingBuffer;
        VkDeviceMemory stagingBufferMemory;
        Utils::VulkanCreateBuffer(
            m_vkState._core.getDevice(), m_vkState._core.getPhysDevice(), uboBufSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, stagingBuffer, stagingBufferMemory);

        void* data;
        vkMapMemory(m_vkState._core.getDevice(), stagingBufferMemory, 0, uboBufSize, 0, &data);
        memcpy(data, &m_ubo.params, (size_t)uboBufSize);
        vkUnmapMemory(m_vkState._core.getDevice(), stagingBufferMemory);

        for (size_t i = 0u; i < VulkanState::MAX_FRAMES_IN_FLIGHT; ++i) {
            Utils::VulkanCreateBuffer(m_vkState._core.getDevice(), m_vkState._core.getPhysDevice(), uboBufSize,
                                      VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, m_ubo.buffers[i], m_ubo.buffersMemory[i]);

            Utils::VulkanCopyBuffer(m_vkState._core.getDevice(), m_vkState._queue, m_vkState._cmdBufPool, stagingBuffer,
                                    m_ubo.buffers[i], uboBufSize);
        }

        vkDestroyBuffer(m_vkState._core.getDevice(), stagingBuffer, nullptr);
        vkFreeMemory(m_vkState._core.getDevice(), stagingBufferMemory, nullptr);
    }

    // noise 4x4 texture with GL_REPEAT mode for random offsets along XY plane
    // where x\y: [-1 1] and y is always zero
    {
        const auto imageFormat = VK_FORMAT_R16G16B16A16_SFLOAT;

        std::vector<glm::vec4> ssaoNoise;
        const uint32_t width = 4u;
        const uint32_t height = 4u;
        for (uint32_t i = 0u; i < width * height; ++i) {
            glm::vec4 noise(randomFloats(generator) * 2.0 - 1.0, randomFloats(generator) * 2.0 - 1.0, 0.0f, 0.0f);
            ssaoNoise.push_back(noise);
        }

        VkDeviceSize imageSize = static_cast<VkDeviceSize>(width * height * 4 * sizeof(float));  // 4 components (rgba)

        m_noiseTexture.width = width;
        m_noiseTexture.height = height;
        m_noiseTexture.mipLevels = 1u;

        VkBuffer stagingBuffer;
        VkDeviceMemory stagingBufferMemory;
        Utils::VulkanCreateBuffer(
            m_vkState._core.getDevice(), m_vkState._core.getPhysDevice(), imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, stagingBuffer, stagingBufferMemory);

        void* data;
        vkMapMemory(m_vkState._core.getDevice(), stagingBufferMemory, 0, imageSize, 0, &data);
        memcpy(data, ssaoNoise.data(), imageSize);
        vkUnmapMemory(m_vkState._core.getDevice(), stagingBufferMemory);

        auto res = Utils::VulkanCreateImage(
            m_vkState._core.getDevice(), m_vkState._core.getPhysDevice(), m_noiseTexture.width, m_noiseTexture.height,
            imageFormat, VK_IMAGE_TILING_OPTIMAL,
            VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, m_noiseTexture.m_textureImage, m_noiseTexture.m_textureImageMemory);

        Utils::VulkanTransitionImageLayout(m_vkState._core.getDevice(), m_vkState._queue, m_vkState._cmdBufPool,
                                           m_noiseTexture.m_textureImage, imageFormat, VK_IMAGE_LAYOUT_UNDEFINED,
                                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);
        Utils::VulkanCopyBufferToImage(m_vkState._core.getDevice(), m_vkState._queue, m_vkState._cmdBufPool, stagingBuffer,
                                       m_noiseTexture.m_textureImage, m_noiseTexture.width, m_noiseTexture.height);

        Utils::VulkanTransitionImageLayout(m_vkState._core.getDevice(), m_vkState._queue, m_vkState._cmdBufPool,
                                           m_noiseTexture.m_textureImage, imageFormat, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                           VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);

        if (Utils::VulkanCreateImageView(m_vkState._core.getDevice(), m_noiseTexture.m_textureImage, imageFormat,
                                         VK_IMAGE_ASPECT_COLOR_BIT, m_noiseTexture.m_textureImageView,
                                         m_noiseTexture.mipLevels) != VK_SUCCESS) {
            Utils::printLog(ERROR_PARAM, "failed to create texture imageView for SSAO");
        }

        vkDestroyBuffer(m_vkState._core.getDevice(), stagingBuffer, nullptr);
        vkFreeMemory(m_vkState._core.getDevice(), stagingBufferMemory, nullptr);

        VkSamplerCreateInfo samplerInfo{};
        samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samplerInfo.magFilter = VK_FILTER_NEAREST;
        samplerInfo.minFilter = VK_FILTER_NEAREST;
        samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        samplerInfo.anisotropyEnable = VK_FALSE;
        samplerInfo.maxAnisotropy = 4;
        samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
        samplerInfo.unnormalizedCoordinates = VK_FALSE;  /// -> [0: 1]
        samplerInfo.compareEnable = VK_FALSE;
        samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
        samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        samplerInfo.minLod = 0.0f;
        samplerInfo.maxLod = 0.0f;
        samplerInfo.mipLodBias = 0.0f;

        if (vkCreateSampler(m_vkState._core.getDevice(), &samplerInfo, nullptr, &mSampler) != VK_SUCCESS) {
            Utils::printLog(ERROR_PARAM, "failed to create texture sampler for SSAO!");
        }
    }
}

void PipelineCreatorSSAO::createDescriptorSetLayout() {
    // G Normal attachment
    VkDescriptorSetLayoutBinding normalInputLayoutBinding{};
    normalInputLayoutBinding.binding = 0;
    normalInputLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
    normalInputLayoutBinding.descriptorCount = 1;
    normalInputLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    // Depth attachment
    VkDescriptorSetLayoutBinding depthInputLayoutBinding{};
    depthInputLayoutBinding.binding = 1;
    depthInputLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    depthInputLayoutBinding.descriptorCount = 1;
    depthInputLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    // Texture
    VkDescriptorSetLayoutBinding samplerLayoutBinding{};
    samplerLayoutBinding.binding = 2;
    samplerLayoutBinding.descriptorCount = 1;
    samplerLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    samplerLayoutBinding.pImmutableSamplers = nullptr;
    samplerLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    // UBO Binding Info
    VkDescriptorSetLayoutBinding UBOLayoutBinding = {};
    UBOLayoutBinding.binding = 3;
    UBOLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    UBOLayoutBinding.descriptorCount = 1;
    UBOLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    UBOLayoutBinding.pImmutableSamplers = nullptr;

    // UBO semisphera kernel for sampling
    VkDescriptorSetLayoutBinding UBOKernelLayoutBinding = UBOLayoutBinding;
    UBOKernelLayoutBinding.binding = 4;

    // View Space Position attachment
    VkDescriptorSetLayoutBinding viewSpacePosLayoutBinding{};
    viewSpacePosLayoutBinding.binding = 5;
    viewSpacePosLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    viewSpacePosLayoutBinding.descriptorCount = 1;
    viewSpacePosLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    std::array<VkDescriptorSetLayoutBinding, 6u> inputBindings{UBOLayoutBinding, samplerLayoutBinding, normalInputLayoutBinding,
                                                               depthInputLayoutBinding, UBOKernelLayoutBinding, viewSpacePosLayoutBinding};

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

void PipelineCreatorSSAO::createDescriptorPool() {
    assert(m_descriptorPool == nullptr);  // avoid multiple alocation of the same pool
    uint32_t descriptorCount = VulkanState::MAX_FRAMES_IN_FLIGHT;

    VkDescriptorPoolSize uboPoolSize{};
    uboPoolSize.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uboPoolSize.descriptorCount = descriptorCount;

    VkDescriptorPoolSize texturePoolSize = uboPoolSize;
    texturePoolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;

    VkDescriptorPoolSize depthInputPoolSize = texturePoolSize;

    VkDescriptorPoolSize gNormalInputPoolSize = texturePoolSize;
    gNormalInputPoolSize.type = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;

    VkDescriptorPoolSize uboKernelPoolSize = uboPoolSize;

    VkDescriptorPoolSize viewSpacePosInputPoolSize = texturePoolSize;

    std::array<VkDescriptorPoolSize, 6u> poolSize{uboPoolSize, texturePoolSize, gNormalInputPoolSize, depthInputPoolSize,
                                                  uboKernelPoolSize, viewSpacePosInputPoolSize};

    VkDescriptorPoolCreateInfo inputPoolCreateInfo = {};
    inputPoolCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    inputPoolCreateInfo.maxSets = descriptorCount;
    inputPoolCreateInfo.poolSizeCount = poolSize.size();
    inputPoolCreateInfo.pPoolSizes = poolSize.data();

    if (vkCreateDescriptorPool(m_vkState._core.getDevice(), &inputPoolCreateInfo, nullptr, &m_descriptorPool) != VK_SUCCESS) {
        Utils::printLog(ERROR_PARAM, "failed to create descriptor pool for second pass!");
    }
    mIsPoolRecreated = true;
}

void PipelineCreatorSSAO::recreateDescriptors() {
    if (!mIsPoolRecreated) {
        Utils::printLog(ERROR_PARAM, "no need to create descriptors!");
        return;
    }
    assert(m_vkState._core.getDevice());
    assert(m_descriptorSetLayout);
    assert(m_noiseTexture.m_textureImageView && mSampler);

    std::vector<VkDescriptorSetLayout> layouts(VulkanState::MAX_FRAMES_IN_FLIGHT, *m_descriptorSetLayout.get());
    // Input Attachment Descriptor Set Allocation Info
    VkDescriptorSetAllocateInfo setAllocInfo = {};
    setAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    setAllocInfo.descriptorPool = m_descriptorPool;
    setAllocInfo.descriptorSetCount = VulkanState::MAX_FRAMES_IN_FLIGHT;
    setAllocInfo.pSetLayouts = layouts.data();

    // Allocate Descriptor Sets
    auto status = vkAllocateDescriptorSets(m_vkState._core.getDevice(), &setAllocInfo, m_descriptorSets.data());
    if (status != VK_SUCCESS) {
        Utils::printLog(ERROR_PARAM, "failed to allocate descriptor sets! ", status);
        return;
    }

    // Update each descriptor set with input attachment
    for (uint32_t i = 0u; i < VulkanState::MAX_FRAMES_IN_FLIGHT; ++i) {
        // G Normal
        VkDescriptorImageInfo imageGNormalInfo{};
        imageGNormalInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageGNormalInfo.imageView = m_vkState._gPassBuffer.normal.colorBufferImageView[i];
        imageGNormalInfo.sampler = VK_NULL_HANDLE;

        // GPass Attachment Descriptor Write
        VkWriteDescriptorSet gNormalWrite = {};
        gNormalWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        gNormalWrite.dstSet = m_descriptorSets[i];
        gNormalWrite.dstBinding = 0;
        gNormalWrite.dstArrayElement = 0;
        gNormalWrite.descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
        gNormalWrite.descriptorCount = 1;
        gNormalWrite.pImageInfo = &imageGNormalInfo;

        // Depth Attachment
        VkDescriptorImageInfo depthAttachmentInfo{};
        depthAttachmentInfo.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        depthAttachmentInfo.imageView = m_vkState._depthBuffer.depthImageView;
        depthAttachmentInfo.sampler = VK_NULL_HANDLE;

        VkWriteDescriptorSet depthWrite{};
        depthWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        depthWrite.dstSet = m_descriptorSets[i];
        depthWrite.dstBinding = 1;
        depthWrite.dstArrayElement = 0;
        depthWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        depthWrite.descriptorCount = 1;
        depthWrite.pImageInfo = &depthAttachmentInfo;

        // Texture
        VkDescriptorImageInfo imageInfo{};
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageInfo.imageView = m_noiseTexture.m_textureImageView;
        imageInfo.sampler = mSampler;

        VkWriteDescriptorSet textureSetWrite = {};
        textureSetWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        textureSetWrite.dstSet = m_descriptorSets[i];
        textureSetWrite.dstBinding = 2;
        textureSetWrite.dstArrayElement = 0;
        textureSetWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        textureSetWrite.descriptorCount = 1;
        textureSetWrite.pImageInfo = &imageInfo;

        // UBO Semisphere kernel for sampling
        VkDescriptorBufferInfo bufferKernelInfo{};
        bufferKernelInfo.buffer = m_ubo.buffers[i];
        bufferKernelInfo.offset = 0;
        bufferKernelInfo.range = sizeof(UBOSemiSpheraKernel::Params);

        VkWriteDescriptorSet uboKernelDescriptorWrite{};
        uboKernelDescriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        uboKernelDescriptorWrite.dstSet = m_descriptorSets[i];
        uboKernelDescriptorWrite.dstBinding = 3;
        uboKernelDescriptorWrite.dstArrayElement = 0;
        uboKernelDescriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        uboKernelDescriptorWrite.descriptorCount = 1;
        uboKernelDescriptorWrite.pBufferInfo = &bufferKernelInfo;

        // UBO MVP DESCRIPTOR
        VkDescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = m_vkState._ubo.buffers[i];
        bufferInfo.offset = 0;
        bufferInfo.range = sizeof(VulkanState::ViewProj);

        VkWriteDescriptorSet uboDescriptorWrite{};
        uboDescriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        uboDescriptorWrite.dstSet = m_descriptorSets[i];
        uboDescriptorWrite.dstBinding = 4;
        uboDescriptorWrite.dstArrayElement = 0;
        uboDescriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        uboDescriptorWrite.descriptorCount = 1;
        uboDescriptorWrite.pBufferInfo = &bufferInfo;

        // View Space Position Attachment
        VkDescriptorImageInfo viewSpacePosInfo{};
        viewSpacePosInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        viewSpacePosInfo.imageView = m_vkState._viewSpaceBuffer.colorBufferImageView[i];
        viewSpacePosInfo.sampler = VK_NULL_HANDLE;

        // View Space Position Attachment Descriptor Write
        VkWriteDescriptorSet viewSpacePosWrite = {};
        viewSpacePosWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        viewSpacePosWrite.dstSet = m_descriptorSets[i];
        viewSpacePosWrite.dstBinding = 5;
        viewSpacePosWrite.dstArrayElement = 0;
        viewSpacePosWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        viewSpacePosWrite.descriptorCount = 1;
        viewSpacePosWrite.pImageInfo = &viewSpacePosInfo;

        std::array<VkWriteDescriptorSet, 6u> descriptorSets{gNormalWrite, depthWrite, textureSetWrite, uboKernelDescriptorWrite,
                                                            uboDescriptorWrite, viewSpacePosWrite};

        // Update descriptor sets
        vkUpdateDescriptorSets(m_vkState._core.getDevice(), descriptorSets.size(), descriptorSets.data(), 0, nullptr);
    }
    mIsPoolRecreated = false;
}
