#include "PipelineCreatorQuad.h"
#include <assert.h>
#include "Utils.h"

void PipelineCreatorQuad::createPipeline(VkRenderPass renderPass) {
    assert(m_descriptorSetLayout);
    assert(renderPass);
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
                                                         *m_descriptorSetLayout.get(), renderPass, m_vkState._core.getDevice(),
                                                         m_subpassAmount, m_pushConstantRange);

    assert(m_pipeline);
}

void PipelineCreatorQuad::createDescriptorSetLayout() {
    // CREATE INPUT ATTACHMENT

    VkDescriptorSetLayoutBinding colourInputLayoutBinding{};
    colourInputLayoutBinding.binding = 0;
    colourInputLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
    colourInputLayoutBinding.descriptorCount = 1;
    colourInputLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    // Array of input attachment bindings
    auto inputBindingsSize = 1 + (m_isGPassNeeded ? m_vkState._gPassBuffer.size : 0) + (m_isDepthNeeded ? 1 : 0);
    std::vector<VkDescriptorSetLayoutBinding> inputBindings(inputBindingsSize, colourInputLayoutBinding);
    for (size_t i = 0u; i < inputBindings.size(); ++i) {
        inputBindings[i].binding = i;
    }

    // UboViewProjection Binding Info
    VkDescriptorSetLayoutBinding uboLayoutBinding{};
    uboLayoutBinding.binding = inputBindings.size();
    uboLayoutBinding.descriptorCount = 1;
    uboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uboLayoutBinding.pImmutableSamplers = nullptr;
    uboLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    inputBindings.push_back(uboLayoutBinding);

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
    depthInputPoolSize.descriptorCount = 1;  // common depth buffer used in each swapchain

    // GPass Color Attachment Pool Size
    VkDescriptorPoolSize gPassColorInputPoolSize = {};
    gPassColorInputPoolSize.type = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
    gPassColorInputPoolSize.descriptorCount =
        static_cast<uint32_t>(m_vkState._gPassBuffer.size * m_vkState._gPassBuffer.normal.colorBufferImageView.size());

    std::vector<VkDescriptorPoolSize> inputPoolSizes = {uboPoolSize, gPassColorInputPoolSize, colorInputPoolSize,
                                                        depthInputPoolSize};

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

void PipelineCreatorQuad::createDescriptor() {
    // Fill array of layouts ready for set creation
    auto descriptorSetLayout = *m_descriptorSetLayout.get();
    std::array<VkDescriptorSetLayout, VulkanState::MAX_FRAMES_IN_FLIGHT> layouts{descriptorSetLayout, descriptorSetLayout,
                                                                                 descriptorSetLayout};

    // Input Attachment Descriptor Set Allocation Info
    VkDescriptorSetAllocateInfo setAllocInfo = {};
    setAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    setAllocInfo.descriptorPool = m_descriptorPool;
    setAllocInfo.descriptorSetCount = VulkanState::MAX_FRAMES_IN_FLIGHT;
    setAllocInfo.pSetLayouts = layouts.data();

    // Allocate Descriptor Sets
    VkResult result = vkAllocateDescriptorSets(m_vkState._core.getDevice(), &setAllocInfo, m_descriptorSets.data());
    CHECK_VULKAN_ERROR("Failed to allocate Input Attachment Descriptor Sets %d", result);

    // Update each descriptor set with input attachment
    for (size_t i = 0u; i < VulkanState::MAX_FRAMES_IN_FLIGHT; ++i) {
        // Color Attachment Descriptor
        VkDescriptorImageInfo colorAttachmentDescriptor = {};
        colorAttachmentDescriptor.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        colorAttachmentDescriptor.imageView = m_vkState._colorBuffer.colorBufferImageView[i];
        colorAttachmentDescriptor.sampler = VK_NULL_HANDLE;

        // Color Attachment Descriptor Write
        VkWriteDescriptorSet colorWrite = {};
        colorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        colorWrite.dstSet = m_descriptorSets[i];
        colorWrite.dstBinding = 0;
        colorWrite.dstArrayElement = 0;
        colorWrite.descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
        colorWrite.descriptorCount = 1;
        colorWrite.pImageInfo = &colorAttachmentDescriptor;

        // List of input descriptor set writes
        std::vector<VkWriteDescriptorSet> setWrites = {colorWrite};

        if (m_isGPassNeeded) {
            // GPass Attachment Descriptor
            VkDescriptorImageInfo normalAttachmentDescriptor{};
            normalAttachmentDescriptor.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            normalAttachmentDescriptor.imageView = m_vkState._gPassBuffer.normal.colorBufferImageView[i];
            normalAttachmentDescriptor.sampler = VK_NULL_HANDLE;

            // GPass Attachment Descriptor Write
            VkWriteDescriptorSet colorWrite = {};
            colorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            colorWrite.dstSet = m_descriptorSets[i];
            colorWrite.dstBinding = 1;
            colorWrite.dstArrayElement = 0;
            colorWrite.descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
            colorWrite.descriptorCount = 1;
            colorWrite.pImageInfo = &normalAttachmentDescriptor;

            setWrites.push_back(colorWrite);

            VkDescriptorImageInfo colorAttachmentDescriptor = normalAttachmentDescriptor;
            colorAttachmentDescriptor.imageView = m_vkState._gPassBuffer.color.colorBufferImageView[i];
            colorWrite.dstBinding = 2;
            colorWrite.pImageInfo = &colorAttachmentDescriptor;
            setWrites.push_back(colorWrite);
        }

        if (m_isDepthNeeded) {
            // Depth Attachment Descriptor
            VkDescriptorImageInfo depthAttachmentDescriptor{};
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

        // Update descriptor sets
        vkUpdateDescriptorSets(m_vkState._core.getDevice(), static_cast<uint32_t>(setWrites.size()), setWrites.data(), 0,
                               nullptr);
    }
}
