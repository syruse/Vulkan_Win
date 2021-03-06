#include "VulkanRenderer.h"
#include <assert.h>
#include <chrono>
#include <limits>

#ifdef _WIN32
#include "Win32Control.h"
///already included 'windows.h' with own implementations of aligned_alloc...
#elif __linux__
#include "XCBControl.h"
#include <cstring> // memcpy
#include <stdlib.h> // aligned_alloc/free
#define _aligned_free free
#define _aligned_malloc aligned_alloc
#else            
///TO DO
#endif 

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE /// coerce the perspective projection matrix to be in depth: [0.0 to 1.0]
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

VulkanRenderer::VulkanRenderer(std::wstring_view appName, size_t width, size_t height)
    : m_width(width)
    , m_height(height)
    ,
#ifdef _WIN32
    m_core(std::make_unique<Win32Control>(appName, width, height))
#elif __linux__
    m_core(std::make_unique<XCBControl>(appName, width, height))
#else            
    ///TO DO
#endif 
{
}

VulkanRenderer::~VulkanRenderer()
{
    cleanupSwapChain();

    _aligned_free(mp_modelTransferSpace);

    for (size_t i = 0; i < m_images.size(); i++) {
        vkDestroyBuffer(m_core.getDevice(), m_uniformBuffers[i], nullptr);
        vkFreeMemory(m_core.getDevice(), m_uniformBuffersMemory[i], nullptr);
        vkDestroyBuffer(m_core.getDevice(), m_dynamicUniformBuffers[i], nullptr);
        vkFreeMemory(m_core.getDevice(), m_dynamicUniformBuffersMemory[i], nullptr);
    }

    vkDestroyDescriptorSetLayout(m_core.getDevice(), m_descriptorSetLayout, nullptr);
    vkDestroyDescriptorSetLayout(m_core.getDevice(), m_descriptorSetLayoutSecondPass, nullptr);

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
    {
        vkDestroySemaphore(m_core.getDevice(), m_presentCompleteSem[i], nullptr);
        vkDestroySemaphore(m_core.getDevice(), m_renderCompleteSem[i], nullptr);
        vkDestroyFence(m_core.getDevice(), m_drawFences[i], nullptr);
    }

    vkDestroyCommandPool(m_core.getDevice(), m_cmdBufPool, nullptr);
}

void VulkanRenderer::cleanupSwapChain()
{
    // Wait until no actions being run on device before destroying
    vkDeviceWaitIdle(m_core.getDevice());

    vkFreeCommandBuffers(m_core.getDevice(), m_cmdBufPool, static_cast<uint32_t>(m_cmdBufs.size()), m_cmdBufs.data());

    vkDestroyImageView(m_core.getDevice(), m_depthImageView, nullptr);
    vkDestroyImage(m_core.getDevice(), m_depthImage, nullptr);
    vkFreeMemory(m_core.getDevice(), m_depthImageMemory, nullptr);

    for (size_t i = 0; i < m_colourBufferImage.size(); ++i)
    {
        vkDestroyImageView(m_core.getDevice(), m_colourBufferImageView[i], nullptr);
        vkDestroyImage(m_core.getDevice(), m_colourBufferImage[i], nullptr);
        vkFreeMemory(m_core.getDevice(), m_colourBufferImageMemory[i], nullptr);
    }

    for (auto framebuffer : m_fbs) {
        vkDestroyFramebuffer(m_core.getDevice(), framebuffer, nullptr);
    }

    for (auto imageView : m_views) {
        vkDestroyImageView(m_core.getDevice(), imageView, nullptr);
    }

    vkDestroySwapchainKHR(m_core.getDevice(), m_swapChainKHR, nullptr);

    m_pipeLine.reset(nullptr);
    m_pipeLineSecondPass.reset(nullptr);
    vkDestroyRenderPass(m_core.getDevice(), m_renderPass, nullptr);

    vkDestroyDescriptorPool(m_core.getDevice(), m_descriptorPool, nullptr);
    vkDestroyDescriptorPool(m_core.getDevice(), m_descriptorPoolSecondPass, nullptr);
}

void VulkanRenderer::recreateSwapChain(uint16_t width, uint16_t height)
{
    INFO_FORMAT(" new width=%d; new height=%d", width, height);
    if (m_width != width || m_height != height)
    {
        cleanupSwapChain();

        m_width = width;
        m_height = height;
        m_currentFrame = 0u;

        createSwapChain();
        createCommandBuffer();
        createDepthResources();
        createColourBufferImage();
        createDescriptorPool();
        recreateDescriptorSets();
        createDescriptorSetsSecondPass();
        createRenderPass();
        createFramebuffer();
        createPipeline();
    }
}

void VulkanRenderer::recreateDescriptorSets()
{
    ///reset
    m_materialId = 0u;
    std::unordered_map<uint16_t, I3DModel::Material> descriptorSets(std::move(m_descriptorSets));
    for (auto& material : descriptorSets)
    {
        m_descriptorCreator(material.second.texture, material.second.sampler);
    }
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
    ubo.view = glm::lookAt(glm::vec3(10.0f, 10.0f, 10.0f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
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
        I3DModel::DynamicUniformBufferObject* pModel = (I3DModel::DynamicUniformBufferObject*)((uint64_t)mp_modelTransferSpace + (i * m_modelUniformAlignment));
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
    m_modelUniformAlignment = (sizeof(I3DModel::DynamicUniformBufferObject) + minUniformBufferOffset - 1)
        & ~(minUniformBufferOffset - 1);

    // Create space in memory to hold dynamic buffer that is aligned to our required alignment and holds MAX_OBJECTS
    mp_modelTransferSpace = (I3DModel::DynamicUniformBufferObject*)_aligned_malloc(m_modelUniformAlignment * MAX_OBJECTS, m_modelUniformAlignment);
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
        Utils::printLog(ERROR_PARAM, "failed to create descriptor set layout!");
    }


    ///---------------------------------------------------------------------------///
    // CREATE INPUT ATTACHMENT IMAGE DESCRIPTOR SET LAYOUT
    // Colour Input Binding
    VkDescriptorSetLayoutBinding colourInputLayoutBinding = {};
    colourInputLayoutBinding.binding = 0;
    colourInputLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
    colourInputLayoutBinding.descriptorCount = 1;
    colourInputLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    // Depth Input Binding
    VkDescriptorSetLayoutBinding depthInputLayoutBinding = {};
    depthInputLayoutBinding.binding = 1;
    depthInputLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
    depthInputLayoutBinding.descriptorCount = 1;
    depthInputLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    // Array of input attachment bindings
    std::vector<VkDescriptorSetLayoutBinding> inputBindings = { colourInputLayoutBinding, depthInputLayoutBinding };

    // Create a descriptor set layout for input attachments
    VkDescriptorSetLayoutCreateInfo inputLayoutCreateInfo = {};
    inputLayoutCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    inputLayoutCreateInfo.bindingCount = static_cast<uint32_t>(inputBindings.size());
    inputLayoutCreateInfo.pBindings = inputBindings.data();

    // Create Descriptor Set Layout
    if (vkCreateDescriptorSetLayout(m_core.getDevice(), &inputLayoutCreateInfo, nullptr, &m_descriptorSetLayoutSecondPass) != VK_SUCCESS) {
        Utils::printLog(ERROR_PARAM, "failed to create descriptor set layout for second pass!");
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
    std::vector<VkDescriptorPoolSize> descriptorPoolSizes {poolSize, modelPoolSize, texturePoolSize};

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = static_cast<uint32_t>(descriptorPoolSizes.size());
    poolInfo.pPoolSizes = descriptorPoolSizes.data();
    poolInfo.maxSets = static_cast<uint32_t>( MAX_FRAMES_IN_FLIGHT * MAX_OBJECTS * 10 ); // Maximum number of Descriptor Sets that can be created from pool

    if (vkCreateDescriptorPool(m_core.getDevice(), &poolInfo, nullptr, &m_descriptorPool) != VK_SUCCESS) {
        Utils::printLog(ERROR_PARAM, "failed to create descriptor pool!");
    }

    ///----------------------------------------------------------------------------------///
    // CREATE INPUT ATTACHMENT DESCRIPTOR POOL
    // Colour Attachment Pool Size
    VkDescriptorPoolSize colourInputPoolSize = {};
    colourInputPoolSize.type = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
    colourInputPoolSize.descriptorCount = static_cast<uint32_t>(m_colourBufferImageView.size());

    // Depth Attachment Pool Size
    VkDescriptorPoolSize depthInputPoolSize = {};
    depthInputPoolSize.type = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
    depthInputPoolSize.descriptorCount = static_cast<uint32_t>(m_images.size());

    std::vector<VkDescriptorPoolSize> inputPoolSizes = { colourInputPoolSize, depthInputPoolSize };

    // Create input attachment pool
    VkDescriptorPoolCreateInfo inputPoolCreateInfo = {};
    inputPoolCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    inputPoolCreateInfo.maxSets = m_images.size();
    inputPoolCreateInfo.poolSizeCount = static_cast<uint32_t>(inputPoolSizes.size());
    inputPoolCreateInfo.pPoolSizes = inputPoolSizes.data();

    if (vkCreateDescriptorPool(m_core.getDevice(), &inputPoolCreateInfo, nullptr, &m_descriptorPoolSecondPass) != VK_SUCCESS) {
    Utils::printLog(ERROR_PARAM, "failed to create descriptor pool for second pass!");
    }
}

void VulkanRenderer::createDescriptorSetsSecondPass()
{
	// Resize array to hold descriptor set for each swap chain image
    m_descriptorSetsSecondPass.resize(m_images.size());

	// Fill array of layouts ready for set creation
	std::vector<VkDescriptorSetLayout> setLayouts(m_images.size(), m_descriptorSetLayoutSecondPass);

	// Input Attachment Descriptor Set Allocation Info
	VkDescriptorSetAllocateInfo setAllocInfo = {};
	setAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	setAllocInfo.descriptorPool = m_descriptorPoolSecondPass;
	setAllocInfo.descriptorSetCount = static_cast<uint32_t>(m_images.size());
	setAllocInfo.pSetLayouts = setLayouts.data();

	// Allocate Descriptor Sets
	VkResult result = vkAllocateDescriptorSets(m_core.getDevice(), &setAllocInfo, m_descriptorSetsSecondPass.data());
    CHECK_VULKAN_ERROR("Failed to allocate Input Attachment Descriptor Sets %d", result);

	// Update each descriptor set with input attachment
	for (size_t i = 0; i < m_images.size(); i++)
	{
		// Colour Attachment Descriptor
		VkDescriptorImageInfo colourAttachmentDescriptor = {};
		colourAttachmentDescriptor.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		colourAttachmentDescriptor.imageView = m_colourBufferImageView[i];
		colourAttachmentDescriptor.sampler = VK_NULL_HANDLE;

		// Colour Attachment Descriptor Write
		VkWriteDescriptorSet colourWrite = {};
		colourWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		colourWrite.dstSet = m_descriptorSetsSecondPass[i];
		colourWrite.dstBinding = 0;
		colourWrite.dstArrayElement = 0;
		colourWrite.descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
		colourWrite.descriptorCount = 1;
		colourWrite.pImageInfo = &colourAttachmentDescriptor;

		// Depth Attachment Descriptor
		VkDescriptorImageInfo depthAttachmentDescriptor = {};
		depthAttachmentDescriptor.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		depthAttachmentDescriptor.imageView = m_depthImageView;
		depthAttachmentDescriptor.sampler = VK_NULL_HANDLE;

		// Depth Attachment Descriptor Write
		VkWriteDescriptorSet depthWrite = {};
		depthWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		depthWrite.dstSet = m_descriptorSetsSecondPass[i];
		depthWrite.dstBinding = 1;
		depthWrite.dstArrayElement = 0;
		depthWrite.descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
		depthWrite.descriptorCount = 1;
		depthWrite.pImageInfo = &depthAttachmentDescriptor;

		// List of input descriptor set writes
		std::vector<VkWriteDescriptorSet> setWrites = { colourWrite, depthWrite };

		// Update descriptor sets
		vkUpdateDescriptorSets(m_core.getDevice(), static_cast<uint32_t>(setWrites.size()), setWrites.data(), 0, nullptr);
	}
}

void VulkanRenderer::createSwapChain()
{
    const VkSurfaceCapabilitiesKHR& SurfaceCaps = m_core.getSurfaceCaps();

    assert(SurfaceCaps.currentExtent.width != -1);

    assert(MAX_FRAMES_IN_FLIGHT >= SurfaceCaps.minImageCount);
    /// <summary>
    /// maxImageCount: value of 0 means that there is no limit on the number of images
    /// </summary>
    if (SurfaceCaps.maxImageCount)
    {
        assert(MAX_FRAMES_IN_FLIGHT <= SurfaceCaps.maxImageCount);
    }

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

    Utils::printLog(INFO_PARAM, "Swap chain created");

    uint32_t NumSwapChainImages = 0;
    res = vkGetSwapchainImagesKHR(m_core.getDevice(), m_swapChainKHR, &NumSwapChainImages, NULL);
    CHECK_VULKAN_ERROR("vkGetSwapchainImagesKHR error %d\n", res);
    assert(MAX_FRAMES_IN_FLIGHT == NumSwapChainImages);
    Utils::printLog(INFO_PARAM, "Number of images ", NumSwapChainImages);

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
        Utils::VulkanCreateBuffer(m_core.getDevice(), m_core.getPhysDevice(), bufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            m_uniformBuffers[i], m_uniformBuffersMemory[i]);
        Utils::VulkanCreateBuffer(m_core.getDevice(), m_core.getPhysDevice(), modelBufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            m_dynamicUniformBuffers[i], m_dynamicUniformBuffersMemory[i]);
    }

    /// uniform buffer updating with a new transformation occurs every frame, so there will be no vkMapMemory here
}

void VulkanRenderer::createCommandPool()
{
    VkCommandPoolCreateInfo cmdPoolCreateInfo = {};
    cmdPoolCreateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    cmdPoolCreateInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT; /// comand buffer will be reset when VkBegin called, so it's resetable now
    cmdPoolCreateInfo.queueFamilyIndex = m_core.getQueueFamily();

    VkResult res = vkCreateCommandPool(m_core.getDevice(), &cmdPoolCreateInfo, NULL, &m_cmdBufPool);
    CHECK_VULKAN_ERROR("vkCreateCommandPool error %d\n", res);

    Utils::printLog(INFO_PARAM, "Command buffer pool created");
}

void VulkanRenderer::createCommandBuffer()
{
    VkCommandBufferAllocateInfo cmdBufAllocInfo = {};
    cmdBufAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmdBufAllocInfo.commandPool = m_cmdBufPool;
    cmdBufAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmdBufAllocInfo.commandBufferCount = m_images.size();

    VkResult res = vkAllocateCommandBuffers(m_core.getDevice(), &cmdBufAllocInfo, &m_cmdBufs[0]);
    CHECK_VULKAN_ERROR("vkAllocateCommandBuffers error %d\n", res);

    Utils::printLog(INFO_PARAM, "Created command buffers");
}

void VulkanRenderer::recordCommandBuffers(uint32_t currentImage)
{
    VkCommandBufferBeginInfo beginInfo = {};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;

    std::array<VkClearValue, 3> clearValues{};
    clearValues[0].color = { 0.0f, 0.0f, 0.0f, 1.0f };
    clearValues[1].color = { 0.2f, 0.0f, 0.0f, 1.0f };
    clearValues[2].depthStencil.depth = 1.0f;

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
    renderPassInfo.clearValueCount = clearValues.size();
    renderPassInfo.pClearValues = clearValues.data();

    //for (uint i = 0; i < m_cmdBufs.size(); i++) {
    VkResult res = vkBeginCommandBuffer(m_cmdBufs[currentImage], &beginInfo);
    CHECK_VULKAN_ERROR("vkBeginCommandBuffer error %d\n", res);
    renderPassInfo.framebuffer = m_fbs[currentImage];

    vkCmdBeginRenderPass(m_cmdBufs[currentImage], &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

    vkCmdBindPipeline(m_cmdBufs[currentImage], VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeLine->pipeline);


    /// For Each mesh start


    // Dynamic Offset Amount
    size_t meshIndex = 0;
    const uint32_t dynamicOffset = static_cast<uint32_t>(m_modelUniformAlignment) * meshIndex;

    vkCmdPushConstants(
        m_cmdBufs[currentImage],
        m_pipeLine->pipelineLayout,
        VK_SHADER_STAGE_VERTEX_BIT,		// Stage to push constants to
        0,								// Offset of push constants to update
        sizeof(PushConstant),		    // Size of data being pushed
        &_pushConstant);		        // Actual data being pushed (can be array)

    /**
    * Unlike vertex and index buffers, descriptor sets are not unique to graphics pipelines.
    * Therefore we need to specify if we want to bind descriptor sets to the graphics or compute pipeline
    */
    // Bind Descriptor Sets
    //vkCmdBindDescriptorSets(m_cmdBufs[currentImage], VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout, 0, 1, &m_descriptorSets[currentImage], 1, &dynamicOffset);

    /** no need to draw over vertices vkCmdDraw(m_cmdBufs[i], static_cast<uint32_t>(vertices.size()), 1, 0, 0); */
    // Execute pipeline

    auto descriptorBinding = [this, currentImage, dynamicOffset](uint16_t materialId)
    {
        vkCmdBindDescriptorSets(m_cmdBufs[currentImage], VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeLine->pipelineLayout, 0, 1,
                                &m_descriptorSets[materialId].descriptorSets[currentImage], 1, &dynamicOffset);
    };
    
    m_objModel.draw(m_cmdBufs[currentImage], descriptorBinding);
    /// For Each mesh end

    ///-----------------------------------------------------------------------------------///
    /// Start second subpass
    vkCmdNextSubpass(m_cmdBufs[currentImage], VK_SUBPASS_CONTENTS_INLINE);

    vkCmdBindPipeline(m_cmdBufs[currentImage], VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeLineSecondPass->pipeline);
    vkCmdBindDescriptorSets(m_cmdBufs[currentImage], VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeLineSecondPass->pipelineLayout,
        0, 1, &m_descriptorSetsSecondPass[currentImage], 0, nullptr);
    vkCmdDraw(m_cmdBufs[currentImage], 6, 1, 0, 0);

    vkCmdEndRenderPass(m_cmdBufs[currentImage]);

    res = vkEndCommandBuffer(m_cmdBufs[currentImage]);
    CHECK_VULKAN_ERROR("vkEndCommandBuffer error %d\n", res);

    Utils::printLog(INFO_PARAM, "Command buffers recorded");
}

void VulkanRenderer::createColourBufferImage()
{
    // Get supported format for colour attachment
    m_colourBufferImage.resize(m_images.size());
    m_colourBufferImageMemory.resize(m_images.size());
    m_colourBufferImageView.resize(m_images.size());

    if (!Utils::VulkanFindSupportedFormat(
        m_core.getPhysDevice(),
        { VK_FORMAT_R8G8B8A8_UNORM },
        VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        m_colourFormat
    ))
    {
        Utils::printLog(ERROR_PARAM, "failed to find supported format!");
    }

    for (size_t i = 0; i < m_images.size(); ++i)
    {
        // Create Colour Buffer Image
        Utils::VulkanCreateImage(m_core.getDevice(), m_core.getPhysDevice(), m_width, m_height, m_colourFormat, VK_IMAGE_TILING_OPTIMAL,
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, m_colourBufferImage[i], m_colourBufferImageMemory[i]);

        // Create Colour Buffer Image View
        Utils::VulkanCreateImageView(m_core.getDevice(), m_colourBufferImage[i], m_colourFormat, VK_IMAGE_ASPECT_COLOR_BIT, m_colourBufferImageView[i]);
    }
}

void VulkanRenderer::loadModels()
{
    m_descriptorCreator = [this](std::weak_ptr<TextureFactory::Texture> texture, VkSampler sampler) -> uint16_t 
    {
        ++m_materialId;

        std::vector<VkDescriptorSetLayout> layouts(m_images.size(), m_descriptorSetLayout);
        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = m_descriptorPool;
        allocInfo.descriptorSetCount = static_cast<uint32_t>(m_images.size());
        allocInfo.pSetLayouts = layouts.data();

        I3DModel::Material material;
        material.sampler = sampler;
        material.texture = texture;
        material.descriptorSets.resize(m_images.size());

        auto status = vkAllocateDescriptorSets(m_core.getDevice(), &allocInfo, material.descriptorSets.data());
        if ( status != VK_SUCCESS && texture.expired())
        {
            Utils::printLog(ERROR_PARAM, "failed to allocate descriptor sets! ", status);
        }
        else
        {
            m_descriptorSets.try_emplace(m_materialId, material);
        }

        // connect the descriptors with buffer when binding

        for (uint16_t i = 0u; i < MAX_FRAMES_IN_FLIGHT; ++i)
        {
            // VIEW PROJECTION DESCRIPTOR
            // Buffer info and data offset info
            VkDescriptorBufferInfo bufferInfo{};
            bufferInfo.buffer = m_uniformBuffers[i];
            bufferInfo.offset = 0;
            bufferInfo.range = sizeof(UniformBufferObject);

            VkWriteDescriptorSet descriptorWrite{};
            descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            descriptorWrite.dstSet = material.descriptorSets[i];
            descriptorWrite.dstBinding = 0;
            descriptorWrite.dstArrayElement = 0;
            descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            descriptorWrite.descriptorCount = 1;
            descriptorWrite.pBufferInfo = &bufferInfo;
            descriptorWrite.pImageInfo = nullptr;       // Optional
            descriptorWrite.pTexelBufferView = nullptr; // Optional

            // MODEL DESCRIPTOR
            // Model Buffer Binding Info
            VkDescriptorBufferInfo modelBufferInfo = {};
            modelBufferInfo.buffer = m_dynamicUniformBuffers[i];
            modelBufferInfo.offset = 0;
            modelBufferInfo.range = m_modelUniformAlignment;

            VkWriteDescriptorSet modelSetWrite = {};
            modelSetWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            modelSetWrite.dstSet = material.descriptorSets[i];
            modelSetWrite.dstBinding = 1;
            modelSetWrite.dstArrayElement = 0;
            modelSetWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
            modelSetWrite.descriptorCount = 1;
            modelSetWrite.pBufferInfo = &modelBufferInfo;

            // Texture
            VkDescriptorImageInfo imageInfo{};
            imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            imageInfo.imageView = texture.lock()->m_textureImageView;
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
            std::vector<VkWriteDescriptorSet> setWrites = {descriptorWrite, modelSetWrite, textureSetWrite};

            // Update the descriptor sets with new buffer/binding info
            vkUpdateDescriptorSets(m_core.getDevice(), static_cast<uint32_t>(setWrites.size()), setWrites.data(),
                                   0, nullptr);
        }

        return m_materialId;
    };

    m_objModel.init(MODEL_PATH.data(), m_core.getDevice(), m_core.getPhysDevice(), m_cmdBufPool, m_queue, m_descriptorCreator);
}

bool VulkanRenderer::renderScene()
{
    bool ret_status = true;
    const auto& winController = m_core.getWinController();
    assert(winController);

    auto windowQueueMSG = winController->processWindowQueueMSGs(); /// falls into NRVO    TO FIX
    ret_status = windowQueueMSG.isQuited;

    if (windowQueueMSG.isResized)
    {
        recreateSwapChain(windowQueueMSG.width, windowQueueMSG.height);
    }
    else if(!windowQueueMSG.isQuited)
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
        if (res == VK_ERROR_OUT_OF_DATE_KHR)
        {
            recreateSwapChain(m_width, m_height);
        }
        else
        {
          CHECK_VULKAN_ERROR("vkQueuePresentKHR error %d\n", res);
        }

        // Get next frame (use % MAX_FRAME_DRAWS to keep value below MAX_FRAME_DRAWS)
        m_currentFrame = ++m_currentFrame % MAX_FRAMES_IN_FLIGHT;
    }

    return ret_status;
}

void VulkanRenderer::createRenderPass()
{
    // Array of our subpasses
    std::array<VkSubpassDescription, 2> subpasses{};

    // ATTACHMENTS
    // SUBPASS 1 ATTACHMENTS + REFERENCES (INPUT ATTACHMENTS)

    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = m_colourFormat;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentDescription depthAttachment{};
    depthAttachment.format = m_depthFormat;
    depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;                       // for more efficiency and since it will not be used after drawing has finished 
    depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference colorAttachmentRef{};
    colorAttachmentRef.attachment = 1;
    colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depthAttachmentRef{};
    depthAttachmentRef.attachment = 2;
    depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    subpasses[0].pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpasses[0].colorAttachmentCount = 1;
    subpasses[0].pColorAttachments = &colorAttachmentRef;
    subpasses[0].pDepthStencilAttachment = &depthAttachmentRef;


    // SUBPASS 2 ATTACHMENTS + REFERENCES

    // Swapchain colour attachment
    VkAttachmentDescription swapchainColourAttachment = {};
    swapchainColourAttachment.format = m_core.getSurfaceFormat().format;					// Format to use for attachment
    swapchainColourAttachment.samples = VK_SAMPLE_COUNT_1_BIT;					            // Number of samples to write for multisampling
    swapchainColourAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;				            // Describes what to do with attachment before rendering
    swapchainColourAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;			            // Describes what to do with attachment after rendering
    swapchainColourAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;	            // Describes what to do with stencil before rendering
    swapchainColourAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;            // Describes what to do with stencil after rendering

    // Framebuffer data will be stored as an image, but images can be given different data layouts
    // to give optimal use for certain operations
    swapchainColourAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;			        // Image data layout before render pass starts
    swapchainColourAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;		        // Image data layout after render pass (to change to)

    // Attachment reference uses an attachment index that refers to index in the attachment list passed to renderPassCreateInfo
    VkAttachmentReference swapchainColourAttachmentReference = {};
    swapchainColourAttachmentReference.attachment = 0;
    swapchainColourAttachmentReference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    // References to attachments that subpass will take input from
    std::array<VkAttachmentReference, 2> inputReferences;
    inputReferences[0].attachment = 1;
    inputReferences[0].layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    inputReferences[1].attachment = 2;
    inputReferences[1].layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    // Set up Subpass 2
    subpasses[1].pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpasses[1].colorAttachmentCount = 1;
    subpasses[1].pColorAttachments = &swapchainColourAttachmentReference;
    subpasses[1].inputAttachmentCount = static_cast<uint32_t>(inputReferences.size());
    subpasses[1].pInputAttachments = inputReferences.data();


    // SUBPASS DEPENDENCIES

    // Need to determine when layout transitions occur using subpass dependencies
    std::array<VkSubpassDependency, 3> subpassDependencies;

    // Conversion from VK_IMAGE_LAYOUT_UNDEFINED to VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
    // Transition must happen after...
    subpassDependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;						          // Subpass index (VK_SUBPASS_EXTERNAL = Special value meaning outside of renderpass)
    subpassDependencies[0].srcStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;		          // Pipeline stage
    subpassDependencies[0].srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;				          // Stage access mask (memory access)
    // But must happen before...
    subpassDependencies[0].dstSubpass = 0;
    subpassDependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    subpassDependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | 
        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
    subpassDependencies[0].dependencyFlags = 0;

    // Subpass 1 layout (colour/depth) to Subpass 2 layout (shader read)
    subpassDependencies[1].srcSubpass = 0;
    subpassDependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    subpassDependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    subpassDependencies[1].dstSubpass = 1;
    subpassDependencies[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    subpassDependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    subpassDependencies[1].dependencyFlags = 0;

    // Conversion from VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL to VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
    // Transition must happen after...
    subpassDependencies[2].srcSubpass = 0;
    subpassDependencies[2].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    subpassDependencies[2].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;;
    // But must happen before...
    subpassDependencies[2].dstSubpass = VK_SUBPASS_EXTERNAL;
    subpassDependencies[2].dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    subpassDependencies[2].dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
    subpassDependencies[2].dependencyFlags = 0;

    std::array<VkAttachmentDescription, 3> renderPassAttachments = { swapchainColourAttachment, colorAttachment, depthAttachment };

    // Create info for Render Pass
    VkRenderPassCreateInfo renderPassCreateInfo = {};
    renderPassCreateInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassCreateInfo.attachmentCount = static_cast<uint32_t>(renderPassAttachments.size());
    renderPassCreateInfo.pAttachments = renderPassAttachments.data();
    renderPassCreateInfo.subpassCount = static_cast<uint32_t>(subpasses.size());
    renderPassCreateInfo.pSubpasses = subpasses.data();
    renderPassCreateInfo.dependencyCount = static_cast<uint32_t>(subpassDependencies.size());
    renderPassCreateInfo.pDependencies = subpassDependencies.data();

    VkResult res = vkCreateRenderPass(m_core.getDevice(), &renderPassCreateInfo, NULL, &m_renderPass);
    CHECK_VULKAN_ERROR("vkCreateRenderPass error %d\n", res);

    Utils::printLog(INFO_PARAM, "Created a render pass");
}

void VulkanRenderer::createFramebuffer()
{
    m_fbs.resize(m_images.size());

    VkResult res;

    for (size_t i = 0; i < m_images.size(); i++) {
        if(Utils::VulkanCreateImageView(m_core.getDevice(), m_images[i], m_core.getSurfaceFormat().format, VK_IMAGE_ASPECT_COLOR_BIT, m_views[i]) != VK_SUCCESS) {
            Utils::printLog(ERROR_PARAM, "failed to create texture image view!");
        }

		std::array<VkImageView, 3> attachments = { m_views[i], m_colourBufferImageView[i], m_depthImageView };

        //The color attachment differs for every swap chain image, 
        //but the same depth image can be used by all of them 
        //because only a single subpass is running at the same time due to our semaphores.

        VkFramebufferCreateInfo fbCreateInfo = {};
        fbCreateInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbCreateInfo.renderPass = m_renderPass;
        fbCreateInfo.attachmentCount = attachments.size();
        fbCreateInfo.pAttachments = attachments.data();
        fbCreateInfo.width = m_width;
        fbCreateInfo.height = m_height;
        fbCreateInfo.layers = 1;

        res = vkCreateFramebuffer(m_core.getDevice(), &fbCreateInfo, NULL, &m_fbs[i]);
        CHECK_VULKAN_ERROR("vkCreateFramebuffer error %d\n", res);
    }

    Utils::printLog(INFO_PARAM, "Frame buffers created");
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
            Utils::printLog(ERROR_PARAM, "Failed to create a Semaphore and/or Fence!");
        }
    }
}

void VulkanRenderer::createPipeline()
{
    m_pipeLine = Pipeliner::getInstance().createPipeLine("vert.spv", "frag.spv", m_width, m_height,
        m_descriptorSetLayout, m_renderPass, m_core.getDevice(), 0u, m_pushConstantRange);
    assert(m_pipeLine);

    ///-----------------------------------------------------------------------------///
    // CREATE SECOND PASS PIPELINE
    // No vertex data for second pass

    auto& vertexInputInfo = Pipeliner::getInstance().getVertexInputInfo();
    vertexInputInfo.vertexBindingDescriptionCount = 0;
    vertexInputInfo.pVertexBindingDescriptions = nullptr;
    vertexInputInfo.vertexAttributeDescriptionCount = 0;
    vertexInputInfo.pVertexAttributeDescriptions = nullptr;

    // Don't want to write to depth buffer
    auto& depthStencil = Pipeliner::getInstance().getDepthStencilInfo();
    depthStencil.depthWriteEnable = VK_FALSE;

    auto& pipelineIACreateInfo = Pipeliner::getInstance().getInputAssemblyInfo();
    pipelineIACreateInfo.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST; // as a simple set with two triangles for quad drawing

    m_pipeLineSecondPass = Pipeliner::getInstance().createPipeLine("vert_secondPass.spv", "frag_secondPass.spv",
        m_width, m_height, m_descriptorSetLayoutSecondPass, m_renderPass, m_core.getDevice(), 1u);

    assert(m_pipeLineSecondPass);
}

void VulkanRenderer::createDepthResources()
{
    if (!Utils::VulkanFindSupportedFormat(
        m_core.getPhysDevice(),
        { VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT },
        VK_IMAGE_TILING_OPTIMAL,
        VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT,
        m_depthFormat
    ))
    {
        Utils::printLog(ERROR_PARAM, "failed to find supported format!");
    }

    Utils::VulkanCreateImage(m_core.getDevice(), m_core.getPhysDevice(), m_width, m_height, m_depthFormat, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, m_depthImage, m_depthImageMemory);
    Utils::VulkanCreateImageView(m_core.getDevice(), m_depthImage, m_depthFormat, VK_IMAGE_ASPECT_DEPTH_BIT, m_depthImageView);

    /// actually it's redundant because it's taken care in render pass
    ///VkImageAspectFlags aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    ///if (m_depthFormat == VK_FORMAT_D32_SFLOAT_S8_UINT || m_depthFormat == VK_FORMAT_D24_UNORM_S8_UINT) {
    ///    aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
    ///}
    ///Utils::VulkanTransitionImageLayout(m_core.getDevice(), m_queue, m_cmdBufPool, m_depthImage, m_depthFormat, 
    ///    VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, aspectMask);
}

void VulkanRenderer::init()
{
    m_core.init();

    vkGetDeviceQueue(m_core.getDevice(), m_core.getQueueFamily(), 0, &m_queue);

    createSwapChain();
    createCommandPool();
    createCommandBuffer();
    createDepthResources();
    createColourBufferImage();
    createDescriptorSetLayout();
    createPushConstantRange();
    allocateDynamicBufferTransferSpace();
    createUniformBuffers();
    createDescriptorPool();
    loadModels();
    createDescriptorSetsSecondPass();
    createRenderPass();
    createFramebuffer();
    createPipeline();
    createSemaphores();
}