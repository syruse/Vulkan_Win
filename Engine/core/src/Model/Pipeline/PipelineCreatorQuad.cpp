#include "PipelineCreatorQuad.h"
#include <assert.h>
#include "Utils.h"

void PipelineCreatorQuad::createPipeline() {
    assert(m_descriptorSetLayout);
    assert(m_renderPass);
    assert(m_vkState._core.getDevice());

    auto& vertexInputInfo = Pipeliner::getInstance().getVertexInputInfo();
    vertexInputInfo.vertexBindingDescriptionCount = 0;
    vertexInputInfo.pVertexBindingDescriptions = nullptr;
    vertexInputInfo.vertexAttributeDescriptionCount = 0;
    vertexInputInfo.pVertexAttributeDescriptions = nullptr;

    // Don't want to write to depth buffer
    auto& depthStencil = Pipeliner::getInstance().getDepthStencilInfo();
    depthStencil.depthWriteEnable = VK_FALSE;

    auto& pipelineIACreateInfo = Pipeliner::getInstance().getInputAssemblyInfo();
    pipelineIACreateInfo.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;  // as a simple set with two triangles for quad drawing

    m_pipeline = Pipeliner::getInstance().createPipeLine(m_vertShader, m_fragShader, m_vkState._width, m_vkState._height,
                                                         *m_descriptorSetLayout.get(), m_renderPass, m_vkState._core.getDevice(),
                                                         m_subpassAmount, m_pushConstantRange);

    assert(m_pipeline);
}

uint32_t PipelineCreatorQuad::getInputBindingsAmount() const {
    uint32_t amount = (m_isGPassNeeded ? (m_vkState._gPassBuffer.size + 1 /*shadowMap*/ + 1 /*ubo*/) : 1 /*singleColorInput*/) +
                      (m_isDepthNeeded ? 1 : 0);
    return amount;
}

void PipelineCreatorQuad::createDescriptorSetLayout() {
    // CREATE INPUT ATTACHMENT

    VkDescriptorSetLayoutBinding colourInputLayoutBinding{};
    colourInputLayoutBinding.binding = 0;
    colourInputLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
    colourInputLayoutBinding.descriptorCount = 1;
    colourInputLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    // Array of input attachment bindings
    auto inputBindingsSize = getInputBindingsAmount();
    std::vector<VkDescriptorSetLayoutBinding> inputBindings(inputBindingsSize, colourInputLayoutBinding);
    for (size_t i = 0u; i < inputBindings.size(); ++i) {
        inputBindings[i].binding = i;
    }

    if (m_isGPassNeeded) {
        // UboViewProjection Binding Info
        VkDescriptorSetLayoutBinding uboLayoutBinding{};
        uboLayoutBinding.binding = inputBindings.size() - 1u;
        uboLayoutBinding.descriptorCount = 1;
        uboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        uboLayoutBinding.pImmutableSamplers = nullptr;
        uboLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        inputBindings.back() = uboLayoutBinding;
    }

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

void PipelineCreatorQuad::createDescriptorPool() {
    // Creation of Input Attachment Descriptor Pool

    // ViewProjection Pool Size
    VkDescriptorPoolSize uboPoolSize{};
    uboPoolSize.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uboPoolSize.descriptorCount = static_cast<uint32_t>(m_vkState._swapChain.images.size());

    // Color Attachment Pool Size
    VkDescriptorPoolSize colorInputPoolSize = {};
    colorInputPoolSize.type = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
    colorInputPoolSize.descriptorCount = static_cast<uint32_t>(m_vkState._colorBuffer.colorBufferImageView.size());

    // Depth Attachment Pool Size
    VkDescriptorPoolSize depthInputPoolSize = {};
    depthInputPoolSize.type = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
    depthInputPoolSize.descriptorCount = VulkanState::MAX_FRAMES_IN_FLIGHT;

    VkDescriptorPoolSize shadowMapInputPoolSize = depthInputPoolSize;
    shadowMapInputPoolSize.descriptorCount = VulkanState::MAX_FRAMES_IN_FLIGHT;

    // GPass Color Attachment Pool Size
    VkDescriptorPoolSize gPassColorInputPoolSize = {};
    gPassColorInputPoolSize.type = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
    gPassColorInputPoolSize.descriptorCount =
        static_cast<uint32_t>(m_vkState._gPassBuffer.size * m_vkState._gPassBuffer.normal.colorBufferImageView.size());

    std::vector<VkDescriptorPoolSize> inputPoolSizes;
    inputPoolSizes.reserve(getInputBindingsAmount());

    if (m_isGPassNeeded) {
        inputPoolSizes.push_back(gPassColorInputPoolSize);
        inputPoolSizes.push_back(shadowMapInputPoolSize);
        inputPoolSizes.push_back(uboPoolSize);
    } else {
        inputPoolSizes.push_back(colorInputPoolSize);
    }

    if (m_isDepthNeeded) {
        inputPoolSizes.push_back(depthInputPoolSize);
    }

    // Create input attachment pool
    VkDescriptorPoolCreateInfo inputPoolCreateInfo = {};
    inputPoolCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    inputPoolCreateInfo.maxSets = m_vkState._swapChain.images.size();
    inputPoolCreateInfo.poolSizeCount = static_cast<uint32_t>(inputPoolSizes.size());
    inputPoolCreateInfo.pPoolSizes = inputPoolSizes.data();

    if (vkCreateDescriptorPool(m_vkState._core.getDevice(), &inputPoolCreateInfo, nullptr, &m_descriptorPool) != VK_SUCCESS) {
        Utils::printLog(ERROR_PARAM, "failed to create descriptor pool for second pass!");
    }
}

void PipelineCreatorQuad::recreateDescriptors() {
    // Fill array of layouts ready for set creation
    auto descriptorSetLayout = *m_descriptorSetLayout.get();
    std::vector<VkDescriptorSetLayout> layouts(VulkanState::MAX_FRAMES_IN_FLIGHT, descriptorSetLayout);
    // Input Attachment Descriptor Set Allocation Info
    VkDescriptorSetAllocateInfo setAllocInfo = {};
    setAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    setAllocInfo.descriptorPool = m_descriptorPool;
    setAllocInfo.descriptorSetCount = VulkanState::MAX_FRAMES_IN_FLIGHT;
    setAllocInfo.pSetLayouts = layouts.data();

    // Allocate Descriptor Sets
    VkResult result = vkAllocateDescriptorSets(m_vkState._core.getDevice(), &setAllocInfo, m_descriptorSets.data());
    CHECK_VULKAN_ERROR("Failed to allocate Input Attachment Descriptor Sets %d", result);

    const auto attachmentsAmount = getInputBindingsAmount();
    // Update each descriptor set with input attachment
    for (size_t i = 0u; i < VulkanState::MAX_FRAMES_IN_FLIGHT; ++i) {
        // List of input descriptor set writes
        std::vector<VkWriteDescriptorSet> setWrites;
        setWrites.reserve(attachmentsAmount);

        VkDescriptorImageInfo normalAttachmentDescriptor, colorAttachmentDescriptor, depthAttachmentDescriptor,
            depthShadowAttachmentDescriptor;
        if (m_isGPassNeeded) {
            // GPass Attachment Descriptor
            normalAttachmentDescriptor.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            normalAttachmentDescriptor.imageView = m_vkState._gPassBuffer.normal.colorBufferImageView[i];
            normalAttachmentDescriptor.sampler = VK_NULL_HANDLE;

            // GPass Attachment Descriptor Write
            VkWriteDescriptorSet colorWrite = {};
            colorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            colorWrite.dstSet = m_descriptorSets[i];
            colorWrite.dstBinding = setWrites.size();
            colorWrite.dstArrayElement = 0;
            colorWrite.descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
            colorWrite.descriptorCount = 1;
            colorWrite.pImageInfo = &normalAttachmentDescriptor;

            setWrites.push_back(colorWrite);

            colorAttachmentDescriptor = normalAttachmentDescriptor;
            colorAttachmentDescriptor.imageView = m_vkState._gPassBuffer.color.colorBufferImageView[i];
            colorWrite.dstBinding = setWrites.size();
            colorWrite.pImageInfo = &colorAttachmentDescriptor;
            setWrites.push_back(colorWrite);
        } else {
            // Color Attachment Descriptor
            colorAttachmentDescriptor.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            colorAttachmentDescriptor.imageView = m_vkState._colorBuffer.colorBufferImageView[i];
            colorAttachmentDescriptor.sampler = VK_NULL_HANDLE;

            // Color Attachment Descriptor Write
            VkWriteDescriptorSet colorWrite = {};
            colorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            colorWrite.dstSet = m_descriptorSets[i];
            colorWrite.dstBinding = setWrites.size();
            colorWrite.dstArrayElement = 0;
            colorWrite.descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
            colorWrite.descriptorCount = 1;
            colorWrite.pImageInfo = &colorAttachmentDescriptor;
            setWrites.push_back(colorWrite);
        }

        if (m_isDepthNeeded) {
            // Depth Attachment Descriptor
            depthAttachmentDescriptor.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
            depthAttachmentDescriptor.imageView = m_vkState._depthBuffer.depthImageView;
            depthAttachmentDescriptor.sampler = VK_NULL_HANDLE;

            // Depth Attachment Descriptor Write
            VkWriteDescriptorSet depthWrite{};
            depthWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            depthWrite.dstSet = m_descriptorSets[i];
            depthWrite.dstBinding = setWrites.size();
            depthWrite.dstArrayElement = 0;
            depthWrite.descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
            depthWrite.descriptorCount = 1;
            depthWrite.pImageInfo = &depthAttachmentDescriptor;

            setWrites.push_back(depthWrite);
        }

        if (m_isGPassNeeded) {
            // Shadow Map
            depthShadowAttachmentDescriptor.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
            depthShadowAttachmentDescriptor.imageView = m_vkState._shadowMapBuffer.depthImageView;
            depthShadowAttachmentDescriptor.sampler = VK_NULL_HANDLE;

            VkWriteDescriptorSet depthWrite{};
            depthWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            depthWrite.dstSet = m_descriptorSets[i];
            depthWrite.dstBinding = setWrites.size();
            depthWrite.dstArrayElement = 0;
            depthWrite.descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
            depthWrite.descriptorCount = 1;
            depthWrite.pImageInfo = &depthShadowAttachmentDescriptor;

            setWrites.push_back(depthWrite);

            // View Projection UBO Descriptor
            VkDescriptorBufferInfo bufferInfo{};
            bufferInfo.buffer = m_vkState._ubo.buffers[i];
            bufferInfo.offset = 0;
            bufferInfo.range = sizeof(VulkanState::ViewProj);

            VkWriteDescriptorSet uboDescriptorWrite{};
            uboDescriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            uboDescriptorWrite.dstSet = m_descriptorSets[i];
            uboDescriptorWrite.dstBinding = setWrites.size();
            uboDescriptorWrite.dstArrayElement = 0;
            uboDescriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            uboDescriptorWrite.descriptorCount = 1;
            uboDescriptorWrite.pBufferInfo = &bufferInfo;
            uboDescriptorWrite.pImageInfo = nullptr;
            uboDescriptorWrite.pTexelBufferView = nullptr;

            setWrites.push_back(uboDescriptorWrite);
        }

        // Update descriptor sets
        vkUpdateDescriptorSets(m_vkState._core.getDevice(), static_cast<uint32_t>(setWrites.size()), setWrites.data(), 0,
                               nullptr);
    }
}
