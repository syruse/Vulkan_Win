#include "VulkanRenderer.h"
#include <assert.h>
#include <chrono>
#include <limits>

#define GLM_FORCE_RADIANS
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>


VulkanRenderer::~VulkanRenderer()
{
    // Wait until no actions being run on device before destroying
    vkDeviceWaitIdle(m_core.getDevice());

    _aligned_free(mp_modelTransferSpace);

    vkDestroyShaderModule(m_core.getDevice(), m_vsModule, nullptr);
    vkDestroyShaderModule(m_core.getDevice(), m_fsModule, nullptr);
    vkDestroyPipeline(m_core.getDevice(), m_pipeline, nullptr);
    vkDestroyPipelineLayout(m_core.getDevice(), m_pipelineLayout, nullptr);
    vkDestroyRenderPass(m_core.getDevice(), m_renderPass, nullptr);

    vkDestroyBuffer(m_core.getDevice(), m_vertexBuffer, nullptr);
    vkFreeMemory(m_core.getDevice(), m_vertexBufferMemory, nullptr);
    vkDestroyBuffer(m_core.getDevice(), m_indexBuffer, nullptr);
    vkFreeMemory(m_core.getDevice(), m_indexBufferMemory, nullptr);
    for (size_t i = 0; i < m_images.size(); i++) {
        vkDestroyBuffer(m_core.getDevice(), m_uniformBuffers[i], nullptr);
        vkFreeMemory(m_core.getDevice(), m_uniformBuffersMemory[i], nullptr);
        vkDestroyBuffer(m_core.getDevice(), m_dynamicUniformBuffers[i], nullptr);
        vkFreeMemory(m_core.getDevice(), m_dynamicUniformBuffersMemory[i], nullptr);
    }

    for (auto framebuffer : m_fbs) {
        vkDestroyFramebuffer(m_core.getDevice(), framebuffer, nullptr);
    }

    for (auto imageView : m_views) {
        vkDestroyImageView(m_core.getDevice(), imageView, nullptr);
    }

    vkDestroySwapchainKHR(m_core.getDevice(), m_swapChainKHR, nullptr);

    vkDestroyDescriptorPool(m_core.getDevice(), m_descriptorPool, nullptr);

    vkDestroySampler(m_core.getDevice(), m_textureSampler, nullptr);
    vkDestroyImageView(m_core.getDevice(), m_textureImageView, nullptr);

    vkDestroyImage(m_core.getDevice(), m_textureImage, nullptr);
    vkFreeMemory(m_core.getDevice(), m_textureImageMemory, nullptr);

    vkDestroyDescriptorSetLayout(m_core.getDevice(), m_descriptorSetLayout, nullptr);

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
    {
        vkDestroySemaphore(m_core.getDevice(), m_presentCompleteSem[i], nullptr);
        vkDestroySemaphore(m_core.getDevice(), m_renderCompleteSem[i], nullptr);
        vkDestroyFence(m_core.getDevice(), m_drawFences[i], nullptr);
    }

    vkDestroyCommandPool(m_core.getDevice(), m_cmdBufPool, nullptr);
}

void VulkanRenderer::createPushConstantRange()
{
    // Define push constant values (no 'create' needed!)
    m_pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;	// Shader stage push constant will go to
    m_pushConstantRange.offset = 0;								// Offset into given data to pass to push constant
    m_pushConstantRange.size = sizeof(PushConstant);						// Size of data being passed
}

void VulkanRenderer::updateUniformBuffer(uint32_t currentImage) {

    assert(m_uniformBuffersMemory.size() > currentImage);

    static auto startTime = std::chrono::high_resolution_clock::now();

    auto currentTime = std::chrono::high_resolution_clock::now();
    float time = std::chrono::duration<float, std::chrono::seconds::period>(currentTime - startTime).count();

    UniformBufferObject ubo{};
    ubo.view = glm::lookAt(glm::vec3(2.0f, 2.0f, 2.0f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    ubo.proj = glm::perspective(glm::radians(65.0f), m_width / (float)m_height, 0.01f, 1000.0f);

    /**
    * GLM was originally designed for OpenGL, where the Y coordinate of the clip coordinates is inverted
    * The easiest way to compensate for that is to flip the sign on the scaling factor of the Y axis in the projection matrix
    */
    ubo.proj[1][1] *= -1;

    // Copy VP data
    void* data;
    vkMapMemory(m_core.getDevice(), m_uniformBuffersMemory[currentImage], 0, sizeof(ubo), 0, &data);
    memcpy(data, &ubo, sizeof(ubo));
    vkUnmapMemory(m_core.getDevice(), m_uniformBuffersMemory[currentImage]);

    // Copy Model data
    for (size_t i = 0; i < MAX_OBJECTS; i++)
    {
        DynamicUniformBufferObject* pModel = (DynamicUniformBufferObject*)((uint64_t)mp_modelTransferSpace + (i * m_modelUniformAlignment));
        pModel->model = glm::rotate(glm::mat4(1.0f), time * glm::radians(90.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    }

    // Map the list of model data
    vkMapMemory(m_core.getDevice(), m_dynamicUniformBuffersMemory[currentImage], 0, m_modelUniformAlignment * MAX_OBJECTS, 0, &data);
    memcpy(data, mp_modelTransferSpace, m_modelUniformAlignment * MAX_OBJECTS);
    vkUnmapMemory(m_core.getDevice(), m_dynamicUniformBuffersMemory[currentImage]);
}

void VulkanRenderer::allocateDynamicBufferTransferSpace()
{
    // Get properties of our new device
    VkPhysicalDeviceProperties deviceProperties;
    vkGetPhysicalDeviceProperties(m_core.getPhysDevice(), &deviceProperties);

    size_t minUniformBufferOffset = static_cast<size_t>(deviceProperties.limits.minUniformBufferOffsetAlignment);

    // Calculate alignment of model data
    m_modelUniformAlignment = (sizeof(DynamicUniformBufferObject) + minUniformBufferOffset - 1)
        & ~(minUniformBufferOffset - 1);

    // Create space in memory to hold dynamic buffer that is aligned to our required alignment and holds MAX_OBJECTS
    mp_modelTransferSpace = (DynamicUniformBufferObject*)_aligned_malloc(m_modelUniformAlignment * MAX_OBJECTS, m_modelUniformAlignment);
}

void VulkanRenderer::createDescriptorSetLayout()
{
    // UboViewProjection Binding Info
    VkDescriptorSetLayoutBinding uboLayoutBinding{};
    uboLayoutBinding.binding = 0;
    uboLayoutBinding.descriptorCount = 1;
    uboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uboLayoutBinding.pImmutableSamplers = nullptr;
    uboLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    // Model Binding Info
    VkDescriptorSetLayoutBinding modelLayoutBinding = {};
    modelLayoutBinding.binding = 1;
    modelLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    modelLayoutBinding.descriptorCount = 1;
    modelLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    modelLayoutBinding.pImmutableSamplers = nullptr;

    // Texture
    VkDescriptorSetLayoutBinding samplerLayoutBinding{};
    samplerLayoutBinding.binding = 2;
    samplerLayoutBinding.descriptorCount = 1;
    samplerLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    samplerLayoutBinding.pImmutableSamplers = nullptr;
    samplerLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    std::vector<VkDescriptorSetLayoutBinding> layoutBindings = { uboLayoutBinding, modelLayoutBinding, samplerLayoutBinding };
    // Create Descriptor Set Layout with given bindings
    VkDescriptorSetLayoutCreateInfo layoutCreateInfo = {};
    layoutCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutCreateInfo.bindingCount = static_cast<uint32_t>(layoutBindings.size());;
    layoutCreateInfo.pBindings = layoutBindings.data();

    if (vkCreateDescriptorSetLayout(m_core.getDevice(), &layoutCreateInfo, nullptr, &m_descriptorSetLayout) != VK_SUCCESS) {
        ERROR("failed to create descriptor set layout!");
    }
}

void VulkanRenderer::createDescriptorPool()
{
    // Type of descriptors + how many DESCRIPTORS, not Descriptor Sets (combined makes the pool size)
    // ViewProjection Pool
    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSize.descriptorCount = static_cast<uint32_t>(m_images.size());

    // Model Pool (DYNAMIC)
    VkDescriptorPoolSize modelPoolSize = {};
    modelPoolSize.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    modelPoolSize.descriptorCount = static_cast<uint32_t>(m_images.size());

    // Texture
    VkDescriptorPoolSize texturePoolSize = {};
    texturePoolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    texturePoolSize.descriptorCount = static_cast<uint32_t>(m_images.size());

    // List of pool sizes
    std::vector<VkDescriptorPoolSize> descriptorPoolSizes = { poolSize, modelPoolSize, texturePoolSize };

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = static_cast<uint32_t>(descriptorPoolSizes.size());		// Amount of Pool Sizes being passed
    poolInfo.pPoolSizes = descriptorPoolSizes.data();                               // Pool Sizes to create pool with
    poolInfo.maxSets = static_cast<uint32_t>(m_images.size());					    // Maximum number of Descriptor Sets that can be created from pool

    if (vkCreateDescriptorPool(m_core.getDevice(), &poolInfo, nullptr, &m_descriptorPool) != VK_SUCCESS) {
        ERROR("failed to create descriptor pool!");
    }
}

void VulkanRenderer::createDescriptorSets()
{
    std::vector<VkDescriptorSetLayout> layouts(m_images.size(), m_descriptorSetLayout);
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = m_descriptorPool;
    allocInfo.descriptorSetCount = static_cast<uint32_t>(m_images.size());
    allocInfo.pSetLayouts = layouts.data();

    m_descriptorSets.resize(m_images.size());
    if (vkAllocateDescriptorSets(m_core.getDevice(), &allocInfo, m_descriptorSets.data()) != VK_SUCCESS) {
        ERROR("failed to allocate descriptor sets!");
    }

    // connect the descriptors with buffer when binding
    for (size_t i = 0; i < m_images.size(); i++) {
        // VIEW PROJECTION DESCRIPTOR
        // Buffer info and data offset info
        VkDescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = m_uniformBuffers[i];
        bufferInfo.offset = 0;
        bufferInfo.range = sizeof(UniformBufferObject);

        VkWriteDescriptorSet descriptorWrite{};
        descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrite.dstSet = m_descriptorSets[i];
        descriptorWrite.dstBinding = 0;
        descriptorWrite.dstArrayElement = 0;
        descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        descriptorWrite.descriptorCount = 1;
        descriptorWrite.pBufferInfo = &bufferInfo;
        descriptorWrite.pImageInfo = nullptr; // Optional
        descriptorWrite.pTexelBufferView = nullptr; // Optional

        // MODEL DESCRIPTOR
        // Model Buffer Binding Info
        VkDescriptorBufferInfo modelBufferInfo = {};
        modelBufferInfo.buffer = m_dynamicUniformBuffers[i];
        modelBufferInfo.offset = 0;
        modelBufferInfo.range = m_modelUniformAlignment;

        VkWriteDescriptorSet modelSetWrite = {};
        modelSetWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        modelSetWrite.dstSet = m_descriptorSets[i];
        modelSetWrite.dstBinding = 1;
        modelSetWrite.dstArrayElement = 0;
        modelSetWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
        modelSetWrite.descriptorCount = 1;
        modelSetWrite.pBufferInfo = &modelBufferInfo;


        // Texture
        VkDescriptorImageInfo imageInfo{};
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageInfo.imageView = m_textureImageView;
        imageInfo.sampler = m_textureSampler;

        VkWriteDescriptorSet textureSetWrite = {};
        textureSetWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        textureSetWrite.dstSet = m_descriptorSets[i];
        textureSetWrite.dstBinding = 2;
        textureSetWrite.dstArrayElement = 0;
        textureSetWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        textureSetWrite.descriptorCount = 1;
        textureSetWrite.pImageInfo = &imageInfo;

        // List of Descriptor Set Writes
        std::vector<VkWriteDescriptorSet> setWrites = { descriptorWrite, modelSetWrite, textureSetWrite };

        // Update the descriptor sets with new buffer/binding info
        vkUpdateDescriptorSets(m_core.getDevice(), static_cast<uint32_t>(setWrites.size()), setWrites.data(),
            0, nullptr);
    }
}

void VulkanRenderer::createSwapChain()
{
    const VkSurfaceCapabilitiesKHR& SurfaceCaps = m_core.getSurfaceCaps();

    assert(SurfaceCaps.currentExtent.width != -1);

    assert(MAX_FRAMES_IN_FLIGHT >= SurfaceCaps.minImageCount);
    assert(MAX_FRAMES_IN_FLIGHT <= SurfaceCaps.maxImageCount);

    VkSwapchainCreateInfoKHR SwapChainCreateInfo = {};

    SwapChainCreateInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    SwapChainCreateInfo.surface = m_core.getSurface();
    SwapChainCreateInfo.minImageCount = MAX_FRAMES_IN_FLIGHT;
    SwapChainCreateInfo.imageFormat = m_core.getSurfaceFormat().format;
    SwapChainCreateInfo.imageColorSpace = m_core.getSurfaceFormat().colorSpace;
    SwapChainCreateInfo.imageExtent = SurfaceCaps.currentExtent;
    SwapChainCreateInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    SwapChainCreateInfo.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    SwapChainCreateInfo.imageArrayLayers = 1;
    SwapChainCreateInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    SwapChainCreateInfo.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    SwapChainCreateInfo.clipped = VK_TRUE;
    SwapChainCreateInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;

    VkResult res = vkCreateSwapchainKHR(m_core.getDevice(), &SwapChainCreateInfo, NULL, &m_swapChainKHR);
    CHECK_VULKAN_ERROR("vkCreateSwapchainKHR error %d\n", res);

    INFO("Swap chain created\n");

    uint32_t NumSwapChainImages = 0;
    res = vkGetSwapchainImagesKHR(m_core.getDevice(), m_swapChainKHR, &NumSwapChainImages, NULL);
    CHECK_VULKAN_ERROR("vkGetSwapchainImagesKHR error %d\n", res);
    assert(MAX_FRAMES_IN_FLIGHT == NumSwapChainImages);
    INFO("Number of images %d\n", NumSwapChainImages);

    m_images.resize(NumSwapChainImages);
    m_views.resize(NumSwapChainImages);
    m_cmdBufs.resize(NumSwapChainImages);

    res = vkGetSwapchainImagesKHR(m_core.getDevice(), m_swapChainKHR, &NumSwapChainImages, &(m_images[0]));
    CHECK_VULKAN_ERROR("vkGetSwapchainImagesKHR error %d\n", res);
}

void VulkanRenderer::createUniformBuffers() 
{
    // ViewProjection buffer size
    VkDeviceSize bufferSize = sizeof(UniformBufferObject);

    // Model buffer size
    VkDeviceSize modelBufferSize = m_modelUniformAlignment * MAX_OBJECTS;

    m_uniformBuffers.resize(m_images.size());
    m_uniformBuffersMemory.resize(m_images.size());
    m_dynamicUniformBuffers.resize(m_images.size());
    m_dynamicUniformBuffersMemory.resize(m_images.size());

    /**
    * We should have multiple buffers, because multiple frames may be in flight at the same time and
    * we don't want to update the buffer in preparation of the next frame while a previous one is still reading from it!
    */
    for (size_t i = 0; i < m_images.size(); i++) {
        Utils::VulkanÑreateBuffer(m_core.getDevice(), m_core.getPhysDevice(), bufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            m_uniformBuffers[i], m_uniformBuffersMemory[i]);
        Utils::VulkanÑreateBuffer(m_core.getDevice(), m_core.getPhysDevice(), modelBufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            m_dynamicUniformBuffers[i], m_dynamicUniformBuffersMemory[i]);
    }

    /// uniform buffer updating with a new transformation occurs every frame, so there will be no vkMapMemory here
}

void VulkanRenderer::createVertexBuffer()
{
    VkDeviceSize bufferSize = sizeof(vertices[0]) * vertices.size();

    VkBuffer stagingBuffer;
    VkDeviceMemory stagingBufferMemory;
    Utils::VulkanÑreateBuffer(m_core.getDevice(), m_core.getPhysDevice(), bufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, stagingBuffer, stagingBufferMemory);

    void* data;
    vkMapMemory(m_core.getDevice(), stagingBufferMemory, 0, bufferSize, 0, &data);
    memcpy(data, vertices.data(), (size_t)bufferSize);
    vkUnmapMemory(m_core.getDevice(), stagingBufferMemory);

    Utils::VulkanÑreateBuffer(m_core.getDevice(), m_core.getPhysDevice(), bufferSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, m_vertexBuffer, m_vertexBufferMemory);

    Utils::VulkanÑopyBuffer(m_core.getDevice(), m_queue, m_cmdBufPool, stagingBuffer, m_vertexBuffer, bufferSize);

    vkDestroyBuffer(m_core.getDevice(), stagingBuffer, nullptr);
    vkFreeMemory(m_core.getDevice(), stagingBufferMemory, nullptr);
}

void VulkanRenderer::createIndexBuffer() 
{
    VkDeviceSize bufferSize = sizeof(indices[0]) * indices.size();

    VkBuffer stagingBuffer;
    VkDeviceMemory stagingBufferMemory;
    Utils::VulkanÑreateBuffer(m_core.getDevice(), m_core.getPhysDevice(), bufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, stagingBuffer, stagingBufferMemory);

    void* data;
    vkMapMemory(m_core.getDevice(), stagingBufferMemory, 0, bufferSize, 0, &data);
    memcpy(data, indices.data(), (size_t)bufferSize);
    vkUnmapMemory(m_core.getDevice(), stagingBufferMemory);

    Utils::VulkanÑreateBuffer(m_core.getDevice(), m_core.getPhysDevice(), bufferSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, m_indexBuffer, m_indexBufferMemory);

    Utils::VulkanÑopyBuffer(m_core.getDevice(), m_queue, m_cmdBufPool, stagingBuffer, m_indexBuffer, bufferSize);

    vkDestroyBuffer(m_core.getDevice(), stagingBuffer, nullptr);
    vkFreeMemory(m_core.getDevice(), stagingBufferMemory, nullptr);
}

void VulkanRenderer::createCommandBuffer()
{
    VkCommandPoolCreateInfo cmdPoolCreateInfo = {};
    cmdPoolCreateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    cmdPoolCreateInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT; /// comand buffer will be reset when VkBegin called, so it's resetable now
    cmdPoolCreateInfo.queueFamilyIndex = m_core.getQueueFamily();

    VkResult res = vkCreateCommandPool(m_core.getDevice(), &cmdPoolCreateInfo, NULL, &m_cmdBufPool);
    CHECK_VULKAN_ERROR("vkCreateCommandPool error %d\n", res);

    INFO("Command buffer pool created\n");

    VkCommandBufferAllocateInfo cmdBufAllocInfo = {};
    cmdBufAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmdBufAllocInfo.commandPool = m_cmdBufPool;
    cmdBufAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmdBufAllocInfo.commandBufferCount = m_images.size();

    res = vkAllocateCommandBuffers(m_core.getDevice(), &cmdBufAllocInfo, &m_cmdBufs[0]);
    CHECK_VULKAN_ERROR("vkAllocateCommandBuffers error %d\n", res);

    INFO("Created command buffers\n");
}

void VulkanRenderer::createTextureImage()
{
    Utils::VulkanCreateTextureImage(m_core.getDevice(), m_core.getPhysDevice(), m_queue, m_cmdBufPool, TEXTURE_FILE_NAME, m_textureImage, m_textureImageMemory);
}

void VulkanRenderer::createTextureImageView()
{
	if (Utils::VulkanCreateImageView(m_core.getDevice(), m_textureImage, VK_FORMAT_R8G8B8A8_SRGB, m_textureImageView) != VK_SUCCESS) {
		ERROR("failed to create texture image view!");
	}
}

void VulkanRenderer::createTextureSampler()
{
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(m_core.getPhysDevice(), &properties);

    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.anisotropyEnable = VK_TRUE;
    samplerInfo.maxAnisotropy = properties.limits.maxSamplerAnisotropy;
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE; /// -> [0: 1]
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;

    if (vkCreateSampler(m_core.getDevice(), &samplerInfo, nullptr, &m_textureSampler) != VK_SUCCESS) {
        ERROR("failed to create texture sampler!");
    }
}

void VulkanRenderer::recordCommandBuffers(uint32_t currentImage)
{
    VkCommandBufferBeginInfo beginInfo = {};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;

    VkClearColorValue clearColor = { 164.0f / 256.0f, 30.0f / 256.0f, 34.0f / 256.0f, 0.0f };
    VkClearValue clearValue = {};
    clearValue.color = clearColor;

    VkImageSubresourceRange imageRange = {};
    imageRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    imageRange.levelCount = 1;
    imageRange.layerCount = 1;

    VkRenderPassBeginInfo renderPassInfo = {};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = m_renderPass;
    renderPassInfo.renderArea.offset.x = 0;
    renderPassInfo.renderArea.offset.y = 0;
    renderPassInfo.renderArea.extent.width = m_width;
    renderPassInfo.renderArea.extent.height = m_height;
    renderPassInfo.clearValueCount = 1;
    renderPassInfo.pClearValues = &clearValue;

    //for (uint i = 0; i < m_cmdBufs.size(); i++) {
    VkResult res = vkBeginCommandBuffer(m_cmdBufs[currentImage], &beginInfo);
    CHECK_VULKAN_ERROR("vkBeginCommandBuffer error %d\n", res);
    renderPassInfo.framebuffer = m_fbs[currentImage];

    vkCmdBeginRenderPass(m_cmdBufs[currentImage], &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

    vkCmdBindPipeline(m_cmdBufs[currentImage], VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);


    /// For Each mesh start
    VkBuffer vertexBuffers[] = { m_vertexBuffer };
    VkDeviceSize offsets[] = { 0 };
    vkCmdBindVertexBuffers(m_cmdBufs[currentImage], 0, 1, vertexBuffers, offsets);
    vkCmdBindIndexBuffer(m_cmdBufs[currentImage], m_indexBuffer, 0, VK_INDEX_TYPE_UINT16);

    // Dynamic Offset Amount
    size_t meshIndex = 0;
    const uint32_t dynamicOffset = static_cast<uint32_t>(m_modelUniformAlignment) * meshIndex;

    vkCmdPushConstants(
        m_cmdBufs[currentImage],
        m_pipelineLayout,
        VK_SHADER_STAGE_VERTEX_BIT,		// Stage to push constants to
        0,								// Offset of push constants to update
        sizeof(PushConstant),		    // Size of data being pushed
        &_pushConstant);		        // Actual data being pushed (can be array)

    /**
    * Unlike vertex and index buffers, descriptor sets are not unique to graphics pipelines.
    * Therefore we need to specify if we want to bind descriptor sets to the graphics or compute pipeline
    */
    // Bind Descriptor Sets
    vkCmdBindDescriptorSets(m_cmdBufs[currentImage], VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout, 0, 1, &m_descriptorSets[currentImage], 1, &dynamicOffset);

    /** no need to draw over vertices vkCmdDraw(m_cmdBufs[i], static_cast<uint32_t>(vertices.size()), 1, 0, 0); */
    // Execute pipeline
    vkCmdDrawIndexed(m_cmdBufs[currentImage], static_cast<uint32_t>(indices.size()), 1, 0, 0, 0);
    /// For Each mesh end

    vkCmdEndRenderPass(m_cmdBufs[currentImage]);

    res = vkEndCommandBuffer(m_cmdBufs[currentImage]);
    CHECK_VULKAN_ERROR("vkEndCommandBuffer error %d\n", res);
    //}

    INFO("Command buffers recorded\n");
}

void VulkanRenderer::renderScene()
{
    // -- GET NEXT IMAGE --
    // Wait for given fence to signal (open) from last draw before continuing
    vkWaitForFences(m_core.getDevice(), 1, &m_drawFences[m_currentFrame], VK_TRUE, UINT64_MAX);
    // Manually reset (close) fences
    vkResetFences(m_core.getDevice(), 1, &m_drawFences[m_currentFrame]);

    uint32_t ImageIndex = 0;
    VkResult res = vkAcquireNextImageKHR(m_core.getDevice(), m_swapChainKHR, UINT64_MAX, m_presentCompleteSem[m_currentFrame], VK_NULL_HANDLE, &ImageIndex);
    CHECK_VULKAN_ERROR("vkAcquireNextImageKHR error %d\n", res);

    VkPipelineStageFlags waitFlags = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submitInfo = {};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &m_cmdBufs[ImageIndex];
    submitInfo.pWaitSemaphores = &m_presentCompleteSem[m_currentFrame];
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitDstStageMask = &waitFlags;
    submitInfo.pSignalSemaphores = &m_renderCompleteSem[m_currentFrame];
    submitInfo.signalSemaphoreCount = 1;

    recordCommandBuffers(ImageIndex); /// added here since now comand buffer is reset after each vkBegin command
    updateUniformBuffer(ImageIndex);

    res = vkQueueSubmit(m_queue, 1, &submitInfo, m_drawFences[m_currentFrame]);
    CHECK_VULKAN_ERROR("vkQueueSubmit error %d\n", res);

    VkPresentInfoKHR presentInfo = {};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &m_swapChainKHR;
    presentInfo.pImageIndices = &ImageIndex;
    presentInfo.pWaitSemaphores = &m_renderCompleteSem[m_currentFrame];
    presentInfo.waitSemaphoreCount = 1;

    res = vkQueuePresentKHR(m_queue, &presentInfo);
    CHECK_VULKAN_ERROR("vkQueuePresentKHR error %d\n", res);

    // Get next frame (use % MAX_FRAME_DRAWS to keep value below MAX_FRAME_DRAWS)
    m_currentFrame = ++m_currentFrame % MAX_FRAMES_IN_FLIGHT;
}

void VulkanRenderer::createRenderPass()
{
    VkAttachmentReference attachRef = {};
    attachRef.attachment = 0;
    attachRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpassDesc = {};
    subpassDesc.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpassDesc.colorAttachmentCount = 1;
    subpassDesc.pColorAttachments = &attachRef;

    VkAttachmentDescription attachDesc = {};
    attachDesc.format = m_core.getSurfaceFormat().format;
    attachDesc.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachDesc.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachDesc.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachDesc.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachDesc.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachDesc.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    attachDesc.samples = VK_SAMPLE_COUNT_1_BIT;

    VkRenderPassCreateInfo renderPassCreateInfo = {};
    renderPassCreateInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassCreateInfo.attachmentCount = 1;
    renderPassCreateInfo.pAttachments = &attachDesc;
    renderPassCreateInfo.subpassCount = 1;
    renderPassCreateInfo.pSubpasses = &subpassDesc;

    VkResult res = vkCreateRenderPass(m_core.getDevice(), &renderPassCreateInfo, NULL, &m_renderPass);
    CHECK_VULKAN_ERROR("vkCreateRenderPass error %d\n", res);

    INFO("Created a render pass\n");
}

void VulkanRenderer::createFramebuffer()
{
    m_fbs.resize(m_images.size());

    VkResult res;

    for (size_t i = 0; i < m_images.size(); i++) {
        if(Utils::VulkanCreateImageView(m_core.getDevice(), m_images[i], m_core.getSurfaceFormat().format, m_views[i]) != VK_SUCCESS) {
            ERROR("failed to create texture image view!");
        }

        VkFramebufferCreateInfo fbCreateInfo = {};
        fbCreateInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbCreateInfo.renderPass = m_renderPass;
        fbCreateInfo.attachmentCount = 1;
        fbCreateInfo.pAttachments = &m_views[i];
        fbCreateInfo.width = m_width;
        fbCreateInfo.height = m_height;
        fbCreateInfo.layers = 1;

        res = vkCreateFramebuffer(m_core.getDevice(), &fbCreateInfo, NULL, &m_fbs[i]);
        CHECK_VULKAN_ERROR("vkCreateFramebuffer error %d\n", res);
    }

    INFO("Frame buffers created\n");
}

void VulkanRenderer::createShaders()
{
    m_vsModule = Utils::VulkanCreateShaderModule(m_core.getDevice(), "vert.spv");
    assert(m_vsModule);

    m_fsModule = Utils::VulkanCreateShaderModule(m_core.getDevice(), "frag.spv");
    assert(m_fsModule);
}


void VulkanRenderer::createSemaphores()
{
    m_presentCompleteSem.resize(MAX_FRAMES_IN_FLIGHT);
    m_renderCompleteSem.resize(MAX_FRAMES_IN_FLIGHT);
    m_drawFences.resize(MAX_FRAMES_IN_FLIGHT);

    // Semaphore creation information
    VkSemaphoreCreateInfo semaphoreCreateInfo = {};
    semaphoreCreateInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    // Fence creation information
    VkFenceCreateInfo fenceCreateInfo = {};
    fenceCreateInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceCreateInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
    {
        if (vkCreateSemaphore(m_core.getDevice(), &semaphoreCreateInfo, nullptr, &m_presentCompleteSem[i]) != VK_SUCCESS ||
            vkCreateSemaphore(m_core.getDevice(), &semaphoreCreateInfo, nullptr, &m_renderCompleteSem[i]) != VK_SUCCESS ||
            vkCreateFence(m_core.getDevice(), &fenceCreateInfo, nullptr, &m_drawFences[i]) != VK_SUCCESS)
        {
            ERROR("Failed to create a Semaphore and/or Fence!");
        }
    }
}

void VulkanRenderer::createPipeline()
{
    VkPipelineShaderStageCreateInfo shaderStageCreateInfo[2] = {};

    shaderStageCreateInfo[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStageCreateInfo[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    shaderStageCreateInfo[0].module = m_vsModule;
    shaderStageCreateInfo[0].pName = "main";
    shaderStageCreateInfo[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStageCreateInfo[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    shaderStageCreateInfo[1].module = m_fsModule;
    shaderStageCreateInfo[1].pName = "main";

    VkPipelineVertexInputStateCreateInfo vertexInputInfo = {};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    auto bindingDescription = Vertex::getBindingDescription();
    auto attributeDescriptions = Vertex::getAttributeDescriptions();

    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
    vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
    vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

    VkPipelineInputAssemblyStateCreateInfo pipelineIACreateInfo = {};
    pipelineIACreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    pipelineIACreateInfo.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;

    VkViewport vp = {};
    vp.x = 0.0f;
    vp.y = 0.0f;
    vp.width = (float)m_width;
    vp.height = (float)m_height;
    vp.minDepth = 0.0f;
    vp.maxDepth = 1.0f;

    VkRect2D scissor;
    scissor.offset.x = 0;
    scissor.offset.y = 0;
    scissor.extent.width = m_width;
    scissor.extent.height = m_height;

    VkPipelineViewportStateCreateInfo vpCreateInfo = {};
    vpCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vpCreateInfo.viewportCount = 1;
    vpCreateInfo.pViewports = &vp;
    vpCreateInfo.scissorCount = 1;
    vpCreateInfo.pScissors = &scissor;

    VkPipelineRasterizationStateCreateInfo rastCreateInfo = {};
    rastCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rastCreateInfo.polygonMode = VK_POLYGON_MODE_FILL;
    rastCreateInfo.cullMode = VK_CULL_MODE_NONE;
    rastCreateInfo.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rastCreateInfo.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo pipelineMSCreateInfo = {};
    pipelineMSCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    pipelineMSCreateInfo.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState blendAttachState = {};
    blendAttachState.colorWriteMask = 0xf;

    VkPipelineColorBlendStateCreateInfo blendCreateInfo = {};
    blendCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    blendCreateInfo.logicOp = VK_LOGIC_OP_COPY;
    blendCreateInfo.attachmentCount = 1;
    blendCreateInfo.pAttachments = &blendAttachState;

    VkPipelineLayoutCreateInfo layoutInfo = {};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &m_descriptorSetLayout;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &m_pushConstantRange;

    VkResult res = vkCreatePipelineLayout(m_core.getDevice(), &layoutInfo, NULL, &m_pipelineLayout);
    CHECK_VULKAN_ERROR("vkCreatePipelineLayout error %d\n", res);

    VkGraphicsPipelineCreateInfo pipelineInfo = {};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = ARRAY_SIZE_IN_ELEMENTS(shaderStageCreateInfo);
    pipelineInfo.pStages = &shaderStageCreateInfo[0];
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &pipelineIACreateInfo;
    pipelineInfo.pViewportState = &vpCreateInfo;
    pipelineInfo.pRasterizationState = &rastCreateInfo;
    pipelineInfo.pMultisampleState = &pipelineMSCreateInfo;
    pipelineInfo.pColorBlendState = &blendCreateInfo;
    pipelineInfo.layout = m_pipelineLayout;
    pipelineInfo.renderPass = m_renderPass;
    pipelineInfo.basePipelineIndex = -1;

    res = vkCreateGraphicsPipelines(m_core.getDevice(), VK_NULL_HANDLE, 1, &pipelineInfo, NULL, &m_pipeline);
    CHECK_VULKAN_ERROR("vkCreateGraphicsPipelines error %d\n", res);

    INFO("Graphics pipeline created\n");
}

void VulkanRenderer::init()
{
    m_core.init();

    vkGetDeviceQueue(m_core.getDevice(), m_core.getQueueFamily(), 0, &m_queue);

    createSwapChain();
    createCommandBuffer();
    createTextureImage();
    createTextureImageView();
    createTextureSampler();
    createVertexBuffer();
    createIndexBuffer();
    createDescriptorSetLayout();
    createPushConstantRange();
    allocateDynamicBufferTransferSpace();
    createUniformBuffers();
    createDescriptorPool();
    createDescriptorSets();
    createRenderPass();
    createFramebuffer();
    createShaders();
    createPipeline();
    createSemaphores();
}