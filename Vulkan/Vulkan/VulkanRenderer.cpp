#include "VulkanRenderer.h"
#include <assert.h>
#include <chrono>
#include <limits>
#include <algorithm>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE /// coerce the perspective projection matrix to be in depth: [0.0 to 1.0]
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "PipelineCreatorTextured.h"
#include "PipelineCreatorSkyBox.h"
#include "PipelineCreatorQuad.h"

#include "ObjModel.h"
#include "Skybox.h"

VulkanRenderer::VulkanRenderer(std::wstring_view appName, size_t width, size_t height)
    : VulkanState(appName, width, height)
    , mTextureFactory(new TextureFactory(*this)) /// this is not used imedially it's safe
{
    assert(mTextureFactory);
    // Define push constant values (no 'create' needed!)
    m_pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;	// Shader stage push constant will go to
    m_pushConstantRange.offset = 0;								    // Offset into given data to pass to push constant
    m_pushConstantRange.size = sizeof(PushConstant);				// Size of data being passed

    m_descriptorCreator = [this](std::weak_ptr<TextureFactory::Texture> texture, VkSampler sampler,
        VkDescriptorSetLayout descriptorSetLayout) -> uint16_t
    {
        assert(descriptorSetLayout);
        assert(!texture.expired());

        ++m_materialId;

        std::vector<VkDescriptorSetLayout> layouts(_swapChainImages.size(), descriptorSetLayout);
        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = m_descriptorPool;
        allocInfo.descriptorSetCount = static_cast<uint32_t>(_swapChainImages.size());
        allocInfo.pSetLayouts = layouts.data();

        I3DModel::Material material;
        material.sampler = sampler;
        material.texture = texture;
        material.descriptorSetLayout = descriptorSetLayout;
        material.descriptorSets.resize(_swapChainImages.size());

        auto status = vkAllocateDescriptorSets(_core.getDevice(), &allocInfo, material.descriptorSets.data());
        if (status != VK_SUCCESS && texture.expired())
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
            std::vector<VkWriteDescriptorSet> setWrites = { descriptorWrite, modelSetWrite, textureSetWrite };

            // Update the descriptor sets with new buffer/binding info
            vkUpdateDescriptorSets(_core.getDevice(), static_cast<uint32_t>(setWrites.size()), setWrites.data(),
                0, nullptr);
        }

        return m_materialId;
    };

    m_pipelineCreators.reserve(3);
    m_pipelineCreators.emplace_back(new PipelineCreatorTextured("vert.spv", "frag.spv", 0u, m_pushConstantRange));
    m_models.emplace_back(new ObjModel(*this, *mTextureFactory, MODEL_PATH, m_pipelineCreators.back().get(), 10U));

    m_pipelineCreators.emplace_back(new PipelineCreatorSkyBox("vert_skybox.spv", "frag_skybox.spv"));
    const std::array<std::string_view, 6> skyBoxTextures
    {
    "dark_ft.png",
    "dark_bk.png",
    "dark_dn.png",
    "dark_up.png",
    "dark_lt.png",
    "dark_rt.png"
    };
    m_models.emplace_back(new Skybox(*this, *mTextureFactory, skyBoxTextures, m_pipelineCreators.back().get()));

    m_pipelineCreators.emplace_back(new PipelineCreatorQuad("vert_secondPass.spv", "frag_secondPass.spv", true, 1u));
    // TO FIX
    m_descriptorSecondPassCreator = [this](const PipelineCreatorBase::descriptor_set_layout_ptr& descriptorSetLayout, VkDescriptorPool descriptorPool, std::vector<VkDescriptorSet>& descriptorsSet, bool isDepthNeeded)
    {
        assert(descriptorSetLayout);
        // Resize array to hold descriptor set for each swap chain image
        descriptorsSet.resize(_swapChainImages.size());

        // Fill array of layouts ready for set creation
        std::vector<VkDescriptorSetLayout> setLayouts(_swapChainImages.size(), *descriptorSetLayout.get());

        // Input Attachment Descriptor Set Allocation Info
        VkDescriptorSetAllocateInfo setAllocInfo = {};
        setAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        setAllocInfo.descriptorPool = descriptorPool;
        setAllocInfo.descriptorSetCount = static_cast<uint32_t>(_swapChainImages.size());
        setAllocInfo.pSetLayouts = setLayouts.data();

        // Allocate Descriptor Sets
        VkResult result = vkAllocateDescriptorSets(_core.getDevice(), &setAllocInfo, descriptorsSet.data());
        CHECK_VULKAN_ERROR("Failed to allocate Input Attachment Descriptor Sets %d", result);

        // Update each descriptor set with input attachment
        for (size_t i = 0; i < _swapChainImages.size(); i++)
        {
            // Color Attachment Descriptor
            VkDescriptorImageInfo colorAttachmentDescriptor = {};
            colorAttachmentDescriptor.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            colorAttachmentDescriptor.imageView = _colorBuffer.colorBufferImageView[i];
            colorAttachmentDescriptor.sampler = VK_NULL_HANDLE;

            // Color Attachment Descriptor Write
            VkWriteDescriptorSet colorWrite = {};
            colorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            colorWrite.dstSet = descriptorsSet[i];
            colorWrite.dstBinding = 0;
            colorWrite.dstArrayElement = 0;
            colorWrite.descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
            colorWrite.descriptorCount = 1;
            colorWrite.pImageInfo = &colorAttachmentDescriptor;

            // List of input descriptor set writes
            std::vector<VkWriteDescriptorSet> setWrites = {colorWrite};
            if (isDepthNeeded) {
                // Depth Attachment Descriptor
                VkDescriptorImageInfo depthAttachmentDescriptor = {};
                depthAttachmentDescriptor.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                depthAttachmentDescriptor.imageView = _depthBuffer.depthImageView;
                depthAttachmentDescriptor.sampler = VK_NULL_HANDLE;

                // Depth Attachment Descriptor Write
                VkWriteDescriptorSet depthWrite = {};
                depthWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                depthWrite.dstSet = descriptorsSet[i];
                depthWrite.dstBinding = 1;
                depthWrite.dstArrayElement = 0;
                depthWrite.descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
                depthWrite.descriptorCount = 1;
                depthWrite.pImageInfo = &depthAttachmentDescriptor;

                setWrites.push_back(depthWrite);
            }

            // Update descriptor sets
            vkUpdateDescriptorSets(_core.getDevice(), static_cast<uint32_t>(setWrites.size()), setWrites.data(), 0, nullptr);
        }
    };

    m_pipelineFXAA = std::make_unique<PipelineCreatorQuad>("vert_fxaa.spv", "frag_fxaa.spv", false);
}

VulkanRenderer::~VulkanRenderer()
{
    Pipeliner::getInstance().saveCache();

    cleanupSwapChain();

    _aligned_free(mp_modelTransferSpace);

    for (size_t i = 0; i < _swapChainImages.size(); i++) {
        vkDestroyBuffer(_core.getDevice(), m_uniformBuffers[i], nullptr);
        vkFreeMemory(_core.getDevice(), m_uniformBuffersMemory[i], nullptr);
        vkDestroyBuffer(_core.getDevice(), m_dynamicUniformBuffers[i], nullptr);
        vkFreeMemory(_core.getDevice(), m_dynamicUniformBuffersMemory[i], nullptr);
    }

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
    {
        vkDestroySemaphore(_core.getDevice(), m_presentCompleteSem[i], nullptr);
        vkDestroySemaphore(_core.getDevice(), m_renderCompleteSem[i], nullptr);
        vkDestroyFence(_core.getDevice(), m_drawFences[i], nullptr);
    }

    vkDestroyCommandPool(_core.getDevice(), _cmdBufPool, nullptr);
}

void VulkanRenderer::cleanupSwapChain()
{
    // Wait until no actions being run on device before destroying
    vkDeviceWaitIdle(_core.getDevice());

    vkFreeCommandBuffers(_core.getDevice(), _cmdBufPool, static_cast<uint32_t>(_cmdBufs.size()), _cmdBufs.data());

    vkDestroyImageView(_core.getDevice(), _depthBuffer.depthImageView, nullptr);
    vkDestroyImage(_core.getDevice(), _depthBuffer.depthImage, nullptr);
    vkFreeMemory(_core.getDevice(), _depthBuffer.depthImageMemory, nullptr);

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
    {
        vkDestroyImageView(_core.getDevice(), _colorBuffer.colorBufferImageView[i], nullptr);
        vkDestroyImage(_core.getDevice(), _colorBuffer.colorBufferImage[i], nullptr);
        vkFreeMemory(_core.getDevice(), _colorBuffer.colorBufferImageMemory[i], nullptr);
    }

    /// force removing before VulkanRenderer removed own data(device)
    mTextureFactory.reset(nullptr);

    for (auto framebuffer : m_fbs) {
        vkDestroyFramebuffer(_core.getDevice(), framebuffer, nullptr);
    }
    for (auto framebuffer : m_fbsFXAA) {
        vkDestroyFramebuffer(_core.getDevice(), framebuffer, nullptr);
    }

    for (auto imageView : _swapChainViews) {
        vkDestroyImageView(_core.getDevice(), imageView, nullptr);
    }

    vkDestroySwapchainKHR(_core.getDevice(), _swapChainKHR, nullptr);

    vkDestroyRenderPass(_core.getDevice(), m_renderPass, nullptr);
    vkDestroyRenderPass(_core.getDevice(), m_renderPassFXAA, nullptr);

    vkDestroyDescriptorPool(_core.getDevice(), m_descriptorPool, nullptr);
    vkDestroyDescriptorPool(_core.getDevice(), m_descriptorPoolSecondPass, nullptr);
    vkDestroyDescriptorPool(_core.getDevice(), m_descriptorPoolFXAApass, nullptr);
}

void VulkanRenderer::recreateSwapChain(uint16_t width, uint16_t height)
{
    INFO_FORMAT(" new width=%d; new height=%d", width, height);
    if (_width != width || _height != height)
    {
        cleanupSwapChain();

        _width = width;
        _height = height;
        m_currentFrame = 0u;

        createSwapChain();
        createCommandBuffer();
        createDepthResources();
        createColorBufferImage();
        createDescriptorPool();
        recreateDescriptorSets();
        createRenderPass();
        createFramebuffer();
        createPipeline();
    }
}

void VulkanRenderer::recreateDescriptorSets()
{
    /// reset
    m_materialId = 0u;
    std::unordered_map<uint16_t, I3DModel::Material> descriptorSets(std::move(m_descriptorSets));
    for (auto& material : descriptorSets)
    {
        m_descriptorCreator(material.second.texture, material.second.sampler, material.second.descriptorSetLayout);
    }

    m_descriptorSecondPassCreator(m_pipelineCreators.back().get()->getDescriptorSetLayout(), m_descriptorPoolSecondPass, m_descriptorSetsSecondPass, true);
    m_descriptorSecondPassCreator(m_pipelineFXAA->getDescriptorSetLayout(), m_descriptorPoolFXAApass, m_descriptorSetsFXAApass, false);
}

void VulkanRenderer::updateUniformBuffer(uint32_t currentImage) {

    assert(m_uniformBuffersMemory.size() > currentImage);

    static auto startTime = std::chrono::high_resolution_clock::now();

    auto currentTime = std::chrono::high_resolution_clock::now();
    float time = std::chrono::duration<float, std::chrono::seconds::period>(currentTime - startTime).count();

    UniformBufferObject ubo{};
    ubo.view = glm::lookAt(glm::vec3(100.0f, 35.0f, 30.0f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    ubo.proj = glm::perspective(glm::radians(65.0f), _width / (float)_height, 0.01f, 1000.0f);

    /**
    * GLM was originally designed for OpenGL, where the Y coordinate of the clip coordinates is inverted
    * The easiest way to compensate for that is to flip the sign on the scaling factor of the Y axis in the projection matrix
    */
    ubo.proj[1][1] *= -1;

    // Copy VP data
    void* data;
    vkMapMemory(_core.getDevice(), m_uniformBuffersMemory[currentImage], 0, sizeof(ubo), 0, &data);
    memcpy(data, &ubo, sizeof(ubo));
    vkUnmapMemory(_core.getDevice(), m_uniformBuffersMemory[currentImage]);

    // Copy Model data
    for (size_t i = 0; i < MAX_OBJECTS; i++)
    {
        I3DModel::DynamicUniformBufferObject* pModel = (I3DModel::DynamicUniformBufferObject*)((uint64_t)mp_modelTransferSpace + (i * m_modelUniformAlignment));
        pModel->model = glm::rotate(glm::mat4(1.0f), time * glm::radians(90.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    }

    // Map the list of model data
    vkMapMemory(_core.getDevice(), m_dynamicUniformBuffersMemory[currentImage], 0, m_modelUniformAlignment * MAX_OBJECTS, 0, &data);
    memcpy(data, mp_modelTransferSpace, m_modelUniformAlignment * MAX_OBJECTS);
    vkUnmapMemory(_core.getDevice(), m_dynamicUniformBuffersMemory[currentImage]);
}

void VulkanRenderer::allocateDynamicBufferTransferSpace()
{
    // Get properties of our new device
    VkPhysicalDeviceProperties deviceProperties;
    vkGetPhysicalDeviceProperties(_core.getPhysDevice(), &deviceProperties);

    size_t minUniformBufferOffset = static_cast<size_t>(deviceProperties.limits.minUniformBufferOffsetAlignment);

    // Calculate alignment of model data
    m_modelUniformAlignment = (sizeof(I3DModel::DynamicUniformBufferObject) + minUniformBufferOffset - 1)
        & ~(minUniformBufferOffset - 1);

    // Create space in memory to hold dynamic buffer that is aligned to our required alignment and holds MAX_OBJECTS
    mp_modelTransferSpace = (I3DModel::DynamicUniformBufferObject*)_aligned_malloc(m_modelUniformAlignment * MAX_OBJECTS, m_modelUniformAlignment);
}

void VulkanRenderer::createDescriptorPool()
{
    // Type of descriptors + how many DESCRIPTORS, not Descriptor Sets (combined makes the pool size)
    // ViewProjection Pool
    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSize.descriptorCount = static_cast<uint32_t>(_swapChainImages.size());

    // Model Pool (DYNAMIC)
    VkDescriptorPoolSize modelPoolSize = {};
    modelPoolSize.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    modelPoolSize.descriptorCount = static_cast<uint32_t>(_swapChainImages.size());

    // Texture
    VkDescriptorPoolSize texturePoolSize = {};
    texturePoolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    texturePoolSize.descriptorCount = static_cast<uint32_t>(_swapChainImages.size());

    // List of pool sizes
    std::vector<VkDescriptorPoolSize> descriptorPoolSizes {poolSize, modelPoolSize, texturePoolSize};

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = static_cast<uint32_t>(descriptorPoolSizes.size());
    poolInfo.pPoolSizes = descriptorPoolSizes.data();
    poolInfo.maxSets = static_cast<uint32_t>( MAX_FRAMES_IN_FLIGHT * MAX_OBJECTS * 10 ); // Maximum number of Descriptor Sets that can be created from pool

    if (vkCreateDescriptorPool(_core.getDevice(), &poolInfo, nullptr, &m_descriptorPool) != VK_SUCCESS) {
        Utils::printLog(ERROR_PARAM, "failed to create descriptor pool!");
    }

    ///----------------------------------------------------------------------------------///
    // CREATE INPUT ATTACHMENT DESCRIPTOR POOL
    // Color Attachment Pool Size
    VkDescriptorPoolSize colorInputPoolSize = {};
    colorInputPoolSize.type = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
    colorInputPoolSize.descriptorCount = static_cast<uint32_t>(_colorBuffer.colorBufferImageView.size());

    // Depth Attachment Pool Size
    VkDescriptorPoolSize depthInputPoolSize = {};
    depthInputPoolSize.type = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
    depthInputPoolSize.descriptorCount = static_cast<uint32_t>(_swapChainImages.size());

    std::vector<VkDescriptorPoolSize> inputPoolSizes = { colorInputPoolSize, depthInputPoolSize };

    // Create input attachment pool
    VkDescriptorPoolCreateInfo inputPoolCreateInfo = {};
    inputPoolCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    inputPoolCreateInfo.maxSets = _swapChainImages.size();
    inputPoolCreateInfo.poolSizeCount = static_cast<uint32_t>(inputPoolSizes.size());
    inputPoolCreateInfo.pPoolSizes = inputPoolSizes.data();

    if (vkCreateDescriptorPool(_core.getDevice(), &inputPoolCreateInfo, nullptr, &m_descriptorPoolSecondPass) != VK_SUCCESS) {
    Utils::printLog(ERROR_PARAM, "failed to create descriptor pool for second pass!");
    }

    // TO FIX hoisting to some function
    ///----------------------------------------------------------------------------------///
    // CREATE INPUT ATTACHMENT DESCRIPTOR POOL
    // Color Attachment Pool Size
    VkDescriptorPoolSize colorInputPoolSizeFXAA = {};
    colorInputPoolSizeFXAA.type = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
    colorInputPoolSizeFXAA.descriptorCount = static_cast<uint32_t>(_colorBuffer.colorBufferImageView.size());

    std::vector<VkDescriptorPoolSize> inputPoolSizesFXAA = { colorInputPoolSizeFXAA };

    // Create input attachment pool
    VkDescriptorPoolCreateInfo inputPoolCreateInfoFXAA = {};
    inputPoolCreateInfoFXAA.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    inputPoolCreateInfoFXAA.maxSets = _swapChainImages.size();
    inputPoolCreateInfoFXAA.poolSizeCount = static_cast<uint32_t>(inputPoolSizesFXAA.size());
    inputPoolCreateInfoFXAA.pPoolSizes = inputPoolSizesFXAA.data();

    if (vkCreateDescriptorPool(_core.getDevice(), &inputPoolCreateInfoFXAA, nullptr, &m_descriptorPoolFXAApass) != VK_SUCCESS) {
        Utils::printLog(ERROR_PARAM, "failed to create descriptor pool for second pass!");
    }
}

void VulkanRenderer::createSwapChain()
{
    const VkSurfaceCapabilitiesKHR& SurfaceCaps = _core.getSurfaceCaps();

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
    SwapChainCreateInfo.surface = _core.getSurface();
    SwapChainCreateInfo.minImageCount = MAX_FRAMES_IN_FLIGHT;
    SwapChainCreateInfo.imageFormat = _core.getSurfaceFormat().format;
    SwapChainCreateInfo.imageColorSpace = _core.getSurfaceFormat().colorSpace;
    SwapChainCreateInfo.imageExtent = SurfaceCaps.currentExtent;
    SwapChainCreateInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    SwapChainCreateInfo.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    SwapChainCreateInfo.imageArrayLayers = 1;
    SwapChainCreateInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    SwapChainCreateInfo.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    SwapChainCreateInfo.clipped = VK_TRUE;
    SwapChainCreateInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;

    VkResult res = vkCreateSwapchainKHR(_core.getDevice(), &SwapChainCreateInfo, NULL, &_swapChainKHR);
    CHECK_VULKAN_ERROR("vkCreateSwapchainKHR error %d\n", res);

    Utils::printLog(INFO_PARAM, "Swap chain created");

    uint32_t NumSwapChainImages = 0;
    res = vkGetSwapchainImagesKHR(_core.getDevice(), _swapChainKHR, &NumSwapChainImages, NULL);
    CHECK_VULKAN_ERROR("vkGetSwapchainImagesKHR error %d\n", res);
    assert(MAX_FRAMES_IN_FLIGHT == NumSwapChainImages);
    Utils::printLog(INFO_PARAM, "Number of images ", NumSwapChainImages);

    res = vkGetSwapchainImagesKHR(_core.getDevice(), _swapChainKHR, &NumSwapChainImages, &(_swapChainImages[0]));
    CHECK_VULKAN_ERROR("vkGetSwapchainImagesKHR error %d\n", res);
}

void VulkanRenderer::createUniformBuffers() 
{
    // ViewProjection buffer size
    VkDeviceSize bufferSize = sizeof(UniformBufferObject);

    // Model buffer size
    VkDeviceSize modelBufferSize = m_modelUniformAlignment * MAX_OBJECTS;

    m_uniformBuffers.resize(_swapChainImages.size());
    m_uniformBuffersMemory.resize(_swapChainImages.size());
    m_dynamicUniformBuffers.resize(_swapChainImages.size());
    m_dynamicUniformBuffersMemory.resize(_swapChainImages.size());

    /**
    * We should have multiple buffers, because multiple frames may be in flight at the same time and
    * we don't want to update the buffer in preparation of the next frame while a previous one is still reading from it!
    */
    for (size_t i = 0; i < _swapChainImages.size(); i++) {
        Utils::VulkanCreateBuffer(_core.getDevice(), _core.getPhysDevice(), bufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            m_uniformBuffers[i], m_uniformBuffersMemory[i]);
        Utils::VulkanCreateBuffer(_core.getDevice(), _core.getPhysDevice(), modelBufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            m_dynamicUniformBuffers[i], m_dynamicUniformBuffersMemory[i]);
    }

    /// uniform buffer updating with a new transformation occurs every frame, so there will be no vkMapMemory here
}

void VulkanRenderer::createCommandPool()
{
    VkCommandPoolCreateInfo cmdPoolCreateInfo = {};
    cmdPoolCreateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    cmdPoolCreateInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT; /// comand buffer will be reset when VkBegin called, so it's resetable now
    cmdPoolCreateInfo.queueFamilyIndex = _core.getQueueFamily();

    VkResult res = vkCreateCommandPool(_core.getDevice(), &cmdPoolCreateInfo, NULL, &_cmdBufPool);
    CHECK_VULKAN_ERROR("vkCreateCommandPool error %d\n", res);

    Utils::printLog(INFO_PARAM, "Command buffer pool created");
}

void VulkanRenderer::createCommandBuffer()
{
    VkCommandBufferAllocateInfo cmdBufAllocInfo = {};
    cmdBufAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmdBufAllocInfo.commandPool = _cmdBufPool;
    cmdBufAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmdBufAllocInfo.commandBufferCount = _swapChainImages.size();

    VkResult res = vkAllocateCommandBuffers(_core.getDevice(), &cmdBufAllocInfo, &_cmdBufs[0]);
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
    clearValues[1].color = { 1.0f, 1.0f, 1.0f, 1.0f };
    clearValues[2].depthStencil.depth = 1.0f;

    VkRenderPassBeginInfo renderPassInfo = {};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = m_renderPass;
    renderPassInfo.renderArea.offset.x = 0;
    renderPassInfo.renderArea.offset.y = 0;
    renderPassInfo.renderArea.extent.width = _width;
    renderPassInfo.renderArea.extent.height = _height;
    renderPassInfo.clearValueCount = clearValues.size();
    renderPassInfo.pClearValues = clearValues.data();

    //for (uint i = 0; i < _cmdBufs.size(); i++) {
    VkResult res = vkBeginCommandBuffer(_cmdBufs[currentImage], &beginInfo);
    CHECK_VULKAN_ERROR("vkBeginCommandBuffer error %d\n", res);
    renderPassInfo.framebuffer = m_fbs[currentImage];

    Utils::VulkanImageMemoryBarrier(_core.getDevice(), _queue, _cmdBufPool, _colorBuffer.colorBufferImage[currentImage], _colorBuffer.colorFormat,
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT, 1U, 1U, 0, 0,
        VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT);

    vkCmdBeginRenderPass(_cmdBufs[currentImage], &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

    // Dynamic Offset Amount
    size_t meshIndex = 0;
    const uint32_t dynamicOffset = static_cast<uint32_t>(m_modelUniformAlignment) * meshIndex;

    ///  SkyBox and 3D Models

    auto descriptorBinding = [this, currentImage, dynamicOffset](uint16_t materialId, VkPipelineLayout pipelineLayout)
    {
        /**
         * Unlike vertex and index buffers, descriptor sets are not unique to graphics pipelines.
         * Therefore we need to specify if we want to bind descriptor sets to the graphics or compute pipeline
         */

        vkCmdBindDescriptorSets(_cmdBufs[currentImage], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1,
            &m_descriptorSets[materialId].descriptorSets[currentImage], 1, &dynamicOffset);
    };

    /* TO FIX pass window size
    vkCmdPushConstants(
        _cmdBufs[currentImage],
        m_pipeLine->pipelineLayout,
        VK_SHADER_STAGE_VERTEX_BIT,		// Stage to push constants to
        0,								// Offset of push constants to update
        sizeof(PushConstant),		    // Size of data being pushed
        &_pushConstant);		        // Actual data being pushed (can be array)
    */

    for(const auto& model: m_models)
    {
        model->draw(_cmdBufs[currentImage], descriptorBinding);
    }


    /// For Each mesh end

    ///-----------------------------------------------------------------------------------///
    /// Start second subpass
    vkCmdNextSubpass(_cmdBufs[currentImage], VK_SUBPASS_CONTENTS_INLINE);

    /// hardcoded m_pipelineCreators.back().get() , FIX this
    vkCmdBindPipeline(_cmdBufs[currentImage], VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineCreators.back().get()->getPipeline()->pipeline);
    vkCmdBindDescriptorSets(_cmdBufs[currentImage], VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineCreators.back().get()->getPipeline()->pipelineLayout,
        0, 1, &m_descriptorSetsSecondPass[currentImage], 0, nullptr);
    vkCmdDraw(_cmdBufs[currentImage], 6, 1, 0, 0);

    vkCmdEndRenderPass(_cmdBufs[currentImage]);


    /// FXAA render pass
    Utils::printLog(INFO_PARAM, "FXAA render pass started");

    VkRenderPassBeginInfo renderPassFXAAInfo = {};
    renderPassFXAAInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassFXAAInfo.renderPass = m_renderPassFXAA;
    renderPassFXAAInfo.renderArea.offset.x = 0;
    renderPassFXAAInfo.renderArea.offset.y = 0;
    renderPassFXAAInfo.renderArea.extent.width = _width;
    renderPassFXAAInfo.renderArea.extent.height = _height;
    renderPassFXAAInfo.clearValueCount = clearValues.size();
    renderPassFXAAInfo.pClearValues = clearValues.data();
    renderPassFXAAInfo.framebuffer = m_fbsFXAA[currentImage];

    Utils::VulkanImageMemoryBarrier(_core.getDevice(), _queue, _cmdBufPool, _colorBuffer.colorBufferImage[currentImage], _colorBuffer.colorFormat,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT, 1U, 1U, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_VERTEX_SHADER_BIT);

    vkCmdBeginRenderPass(_cmdBufs[currentImage], &renderPassFXAAInfo, VK_SUBPASS_CONTENTS_INLINE);

    vkCmdBindPipeline(_cmdBufs[currentImage], VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineFXAA.get()->getPipeline()->pipeline);
    vkCmdBindDescriptorSets(_cmdBufs[currentImage], VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineFXAA.get()->getPipeline()->pipelineLayout,
        0, 1, &m_descriptorSetsFXAApass[currentImage], 0, nullptr);
    vkCmdDraw(_cmdBufs[currentImage], 6, 1, 0, 0);

    vkCmdEndRenderPass(_cmdBufs[currentImage]);

    res = vkEndCommandBuffer(_cmdBufs[currentImage]);
    CHECK_VULKAN_ERROR("vkEndCommandBuffer error %d\n", res);

    Utils::VulkanImageMemoryBarrier(_core.getDevice(), _queue, _cmdBufPool, _colorBuffer.colorBufferImage[currentImage], _colorBuffer.colorFormat,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_IMAGE_ASPECT_COLOR_BIT, 1U, 1U, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, 0,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);

    Utils::printLog(INFO_PARAM, "Command buffers recorded");
}

void VulkanRenderer::createColorBufferImage()
{
    // Get supported format for color attachment

    if (!Utils::VulkanFindSupportedFormat(
        _core.getPhysDevice(),
        { VK_FORMAT_R8G8B8A8_UNORM },
        VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        _colorBuffer.colorFormat
    ))
    {
        Utils::printLog(ERROR_PARAM, "failed to find supported format!");
    }

    for (size_t i = 0; i < _swapChainImages.size(); ++i)
    {
        // Create Color Buffer Image
        Utils::VulkanCreateImage(_core.getDevice(), _core.getPhysDevice(), _width, _height, _colorBuffer.colorFormat, VK_IMAGE_TILING_OPTIMAL,
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 
            _colorBuffer.colorBufferImage[i], _colorBuffer.colorBufferImageMemory[i]);

        // Create Color Buffer Image View
        Utils::VulkanCreateImageView(_core.getDevice(), _colorBuffer.colorBufferImage[i], _colorBuffer.colorFormat, VK_IMAGE_ASPECT_COLOR_BIT, 
            _colorBuffer.colorBufferImageView[i]);
    }
}

void VulkanRenderer::loadModels()
{
    /// lazy init when core is ready
    mTextureFactory->init();

    std::for_each(m_models.begin(), m_models.end(), 
    [&descriptorCreator = m_descriptorCreator]
    (const std::unique_ptr<I3DModel>& pModel)
    {
        assert(pModel);
        assert(descriptorCreator);
        pModel->init(descriptorCreator);
    });

    // TO FIX
    m_descriptorSecondPassCreator(m_pipelineCreators.back().get()->getDescriptorSetLayout(), m_descriptorPoolSecondPass, m_descriptorSetsSecondPass, true);
    m_descriptorSecondPassCreator(m_pipelineFXAA->getDescriptorSetLayout(), m_descriptorPoolFXAApass, m_descriptorSetsFXAApass, false);
}

bool VulkanRenderer::renderScene()
{
    bool ret_status = true;
    const auto& winController = _core.getWinController();
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
        vkWaitForFences(_core.getDevice(), 1, &m_drawFences[m_currentFrame], VK_TRUE, UINT64_MAX);
        // Manually reset (close) fences
        vkResetFences(_core.getDevice(), 1, &m_drawFences[m_currentFrame]);

        uint32_t ImageIndex = 0;
        VkResult res = vkAcquireNextImageKHR(_core.getDevice(), _swapChainKHR, UINT64_MAX, m_presentCompleteSem[m_currentFrame], VK_NULL_HANDLE, &ImageIndex);
        CHECK_VULKAN_ERROR("vkAcquireNextImageKHR error %d\n", res);

        VkPipelineStageFlags waitFlags = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSubmitInfo submitInfo = {};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &_cmdBufs[ImageIndex];
        submitInfo.pWaitSemaphores = &m_presentCompleteSem[m_currentFrame];
        submitInfo.waitSemaphoreCount = 1;
        submitInfo.pWaitDstStageMask = &waitFlags;
        submitInfo.pSignalSemaphores = &m_renderCompleteSem[m_currentFrame];
        submitInfo.signalSemaphoreCount = 1;

        recordCommandBuffers(ImageIndex); /// added here since now comand buffer is reset after each vkBegin command
        updateUniformBuffer(ImageIndex);

        res = vkQueueSubmit(_queue, 1, &submitInfo, m_drawFences[m_currentFrame]);
        CHECK_VULKAN_ERROR("vkQueueSubmit error %d\n", res);

        VkPresentInfoKHR presentInfo = {};
        presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        presentInfo.swapchainCount = 1;
        presentInfo.pSwapchains = &_swapChainKHR;
        presentInfo.pImageIndices = &ImageIndex;
        presentInfo.pWaitSemaphores = &m_renderCompleteSem[m_currentFrame];
        presentInfo.waitSemaphoreCount = 1;

        res = vkQueuePresentKHR(_queue, &presentInfo);
        if (res == VK_ERROR_OUT_OF_DATE_KHR)
        {
            recreateSwapChain(_width, _height);
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
// TO FIX common code getting into functions
void VulkanRenderer::createRenderPass()
{
    // Array of our subpasses
    std::array<VkSubpassDescription, 2> subpasses{};

    // ATTACHMENTS
    // SUBPASS 1 ATTACHMENTS + REFERENCES (INPUT ATTACHMENTS)

    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = _colorBuffer.colorFormat;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentDescription depthAttachment{};
    depthAttachment.format = _depthBuffer.depthFormat;
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

    // Swapchain color attachment
    VkAttachmentDescription swapchainColorAttachment = {};
    swapchainColorAttachment.format = _core.getSurfaceFormat().format;					// Format to use for attachment
    swapchainColorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;					            // Number of samples to write for multisampling
    swapchainColorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;				            // Describes what to do with attachment before rendering
    swapchainColorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;			            // Describes what to do with attachment after rendering
    swapchainColorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;	            // Describes what to do with stencil before rendering
    swapchainColorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;            // Describes what to do with stencil after rendering

    // Framebuffer data will be stored as an image, but images can be given different data layouts
    // to give optimal use for certain operations
    swapchainColorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;			        // Image data layout before render pass starts
    swapchainColorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;		        // Image data layout after render pass (to change to)

    // Attachment reference uses an attachment index that refers to index in the attachment list passed to renderPassCreateInfo
    VkAttachmentReference swapchainColorAttachmentReference = {};
    swapchainColorAttachmentReference.attachment = 0;
    swapchainColorAttachmentReference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    // References to attachments that subpass will take input from
    std::array<VkAttachmentReference, 2> inputReferences;
    inputReferences[0].attachment = 1;
    inputReferences[0].layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    inputReferences[1].attachment = 2;
    inputReferences[1].layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    // Set up Subpass 2
    subpasses[1].pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpasses[1].colorAttachmentCount = 1;
    subpasses[1].pColorAttachments = &swapchainColorAttachmentReference;
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

    // Subpass 1 layout (color/depth) to Subpass 2 layout (shader read)
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

    std::array<VkAttachmentDescription, 3> renderPassAttachments = { swapchainColorAttachment, colorAttachment, depthAttachment };

    // Create info for Render Pass
    VkRenderPassCreateInfo renderPassCreateInfo = {};
    renderPassCreateInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassCreateInfo.attachmentCount = static_cast<uint32_t>(renderPassAttachments.size());
    renderPassCreateInfo.pAttachments = renderPassAttachments.data();
    renderPassCreateInfo.subpassCount = static_cast<uint32_t>(subpasses.size());
    renderPassCreateInfo.pSubpasses = subpasses.data();
    renderPassCreateInfo.dependencyCount = static_cast<uint32_t>(subpassDependencies.size());
    renderPassCreateInfo.pDependencies = subpassDependencies.data();

    VkResult res = vkCreateRenderPass(_core.getDevice(), &renderPassCreateInfo, NULL, &m_renderPass);
    CHECK_VULKAN_ERROR("vkCreateRenderPass error %d\n", res);


    // Create info for Render Pass FXAA
    VkAttachmentDescription ColorAttachmentFXAA = {};
    ColorAttachmentFXAA.format = _core.getSurfaceFormat().format;					// Format to use for attachment
    ColorAttachmentFXAA.samples = VK_SAMPLE_COUNT_1_BIT;					            // Number of samples to write for multisampling
    ColorAttachmentFXAA.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;				            // Describes what to do with attachment before rendering
    ColorAttachmentFXAA.storeOp = VK_ATTACHMENT_STORE_OP_STORE;			            // Describes what to do with attachment after rendering
    ColorAttachmentFXAA.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;	            // Describes what to do with stencil before rendering
    ColorAttachmentFXAA.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;            // Describes what to do with stencil after rendering

    // Framebuffer data will be stored as an image, but images can be given different data layouts
    // to give optimal use for certain operations
    ColorAttachmentFXAA.initialLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;			        // Image data layout before render pass starts
    ColorAttachmentFXAA.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;		        // Image data layout after render pass (to change to)

    // Attachment reference uses an attachment index that refers to index in the attachment list passed to renderPassCreateInfo
    VkAttachmentReference swapchainColorAttachmentReferenceFXAA = {};
    swapchainColorAttachmentReferenceFXAA.attachment = 0;
    swapchainColorAttachmentReferenceFXAA.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    // References to attachments that subpass will take input from
    std::array<VkAttachmentReference, 1> inputReferencesFXAA;
    inputReferencesFXAA[0].attachment = 1;
    inputReferencesFXAA[0].layout = VK_IMAGE_LAYOUT_SHARED_PRESENT_KHR;

    std::array<VkSubpassDescription, 1> subpassesFXAA{};
    subpassesFXAA[0].pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpassesFXAA[0].colorAttachmentCount = 1;
    subpassesFXAA[0].pColorAttachments = &swapchainColorAttachmentReferenceFXAA;
    subpassesFXAA[0].inputAttachmentCount = static_cast<uint32_t>(inputReferencesFXAA.size());
    subpassesFXAA[0].pInputAttachments = inputReferencesFXAA.data();

    std::array<VkAttachmentDescription, 2> renderPassAttachmentsFXAA = { ColorAttachmentFXAA, colorAttachment };

    // Subpass dependencies for layout transitions
    std::array<VkSubpassDependency, 1> dependenciesFXAA;

    dependenciesFXAA[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    dependenciesFXAA[0].dstSubpass = 0;
    dependenciesFXAA[0].srcStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    dependenciesFXAA[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependenciesFXAA[0].srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
    dependenciesFXAA[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dependenciesFXAA[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

    VkRenderPassCreateInfo renderPassCreateInfoFXAA = {};
    renderPassCreateInfoFXAA.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassCreateInfoFXAA.attachmentCount = static_cast<uint32_t>(renderPassAttachmentsFXAA.size());
    renderPassCreateInfoFXAA.pAttachments = renderPassAttachmentsFXAA.data();
    renderPassCreateInfoFXAA.subpassCount = 1;
    renderPassCreateInfoFXAA.pSubpasses = subpassesFXAA.data();
    renderPassCreateInfoFXAA.dependencyCount = static_cast<uint32_t>(dependenciesFXAA.size());
    renderPassCreateInfoFXAA.pDependencies = dependenciesFXAA.data();

    res = vkCreateRenderPass(_core.getDevice(), &renderPassCreateInfoFXAA, NULL, &m_renderPassFXAA);
    CHECK_VULKAN_ERROR("vkCreateRenderPass error %d\n", res);

    Utils::printLog(INFO_PARAM, "Created a render pass");
}

void VulkanRenderer::createFramebuffer()
{
    m_fbs.resize(_swapChainImages.size());

    VkResult res;

    for (size_t i = 0; i < _swapChainImages.size(); i++) {
        if(Utils::VulkanCreateImageView(_core.getDevice(), _swapChainImages[i], _core.getSurfaceFormat().format, VK_IMAGE_ASPECT_COLOR_BIT, _swapChainViews[i]) != VK_SUCCESS) {
            Utils::printLog(ERROR_PARAM, "failed to create texture image view!");
        }

		std::array<VkImageView, 3> attachments = { _swapChainViews[i], _colorBuffer.colorBufferImageView[i], _depthBuffer.depthImageView };

        //The color attachment differs for every swap chain image, 
        //but the same depth image can be used by all of them 
        //because only a single subpass is running at the same time due to our semaphores.

        VkFramebufferCreateInfo fbCreateInfo = {};
        fbCreateInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbCreateInfo.renderPass = m_renderPass;
        fbCreateInfo.attachmentCount = attachments.size();
        fbCreateInfo.pAttachments = attachments.data();
        fbCreateInfo.width = _width;
        fbCreateInfo.height = _height;
        fbCreateInfo.layers = 1;

        res = vkCreateFramebuffer(_core.getDevice(), &fbCreateInfo, NULL, &m_fbs[i]);
        CHECK_VULKAN_ERROR("vkCreateFramebuffer error %d\n", res);
    }

    m_fbsFXAA.resize(_swapChainImages.size());

    for (size_t i = 0; i < _swapChainImages.size(); i++) {
        if (Utils::VulkanCreateImageView(_core.getDevice(), _swapChainImages[i], _core.getSurfaceFormat().format, VK_IMAGE_ASPECT_COLOR_BIT, _swapChainViews[i]) != VK_SUCCESS) {
            Utils::printLog(ERROR_PARAM, "failed to create texture image view!");
        }

        std::array<VkImageView, 2> attachments = { _swapChainViews[i], _colorBuffer.colorBufferImageView[i] };

        //The color attachment differs for every swap chain image, 
        //but the same depth image can be used by all of them 
        //because only a single subpass is running at the same time due to our semaphores.

        VkFramebufferCreateInfo fbCreateInfo = {};
        fbCreateInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbCreateInfo.renderPass = m_renderPassFXAA;
        fbCreateInfo.attachmentCount = attachments.size();
        fbCreateInfo.pAttachments = attachments.data();
        fbCreateInfo.width = _width;
        fbCreateInfo.height = _height;
        fbCreateInfo.layers = 1;

        res = vkCreateFramebuffer(_core.getDevice(), &fbCreateInfo, NULL, &m_fbsFXAA[i]);
        CHECK_VULKAN_ERROR("vkCreateFramebuffer FXAA error %d\n", res);
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
        if (vkCreateSemaphore(_core.getDevice(), &semaphoreCreateInfo, nullptr, &m_presentCompleteSem[i]) != VK_SUCCESS ||
            vkCreateSemaphore(_core.getDevice(), &semaphoreCreateInfo, nullptr, &m_renderCompleteSem[i]) != VK_SUCCESS ||
            vkCreateFence(_core.getDevice(), &fenceCreateInfo, nullptr, &m_drawFences[i]) != VK_SUCCESS)
        {
            Utils::printLog(ERROR_PARAM, "Failed to create a Semaphore and/or Fence!");
        }
    }
}

void VulkanRenderer::createPipeline()
{
    std::for_each(m_pipelineCreators.begin(), m_pipelineCreators.end(), 
    [width = _width, height = _height, renderPass = m_renderPass, device = _core.getDevice()](const std::unique_ptr<PipelineCreatorBase>& pipelineCreator)
    {
        assert(renderPass);
        assert(device);
        pipelineCreator.get()->recreate(width, height, renderPass, device);
    });

    m_pipelineFXAA.get()->recreate(_width, _height, m_renderPassFXAA, _core.getDevice());
}

void VulkanRenderer::createDepthResources()
{
    if (!Utils::VulkanFindSupportedFormat(
        _core.getPhysDevice(),
        { VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT },
        VK_IMAGE_TILING_OPTIMAL,
        VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT,
        _depthBuffer.depthFormat
    ))
    {
        Utils::printLog(ERROR_PARAM, "failed to find supported format!");
    }

    Utils::VulkanCreateImage(_core.getDevice(), _core.getPhysDevice(), _width, _height, _depthBuffer.depthFormat, VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 
        _depthBuffer.depthImage, _depthBuffer.depthImageMemory);
    Utils::VulkanCreateImageView(_core.getDevice(), _depthBuffer.depthImage, _depthBuffer.depthFormat, VK_IMAGE_ASPECT_DEPTH_BIT, _depthBuffer.depthImageView);

    /// actually it's redundant because it's taken care in render pass
    ///VkImageAspectFlags aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    ///if (depthFormat == VK_FORMAT_D32_SFLOAT_S8_UINT || depthFormat == VK_FORMAT_D24_UNORM_S8_UINT) {
    ///    aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
    ///}
    ///Utils::VulkanTransitionImageLayout(_core.getDevice(), _queue, _cmdBufPool, depthImage, depthFormat, 
    ///    VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, aspectMask);
}

void VulkanRenderer::init()
{
    _core.init();

    vkGetDeviceQueue(_core.getDevice(), _core.getQueueFamily(), 0, &_queue);

    createSwapChain();
    createCommandPool();
    createCommandBuffer();
    createDepthResources();
    createColorBufferImage();
    allocateDynamicBufferTransferSpace();
    createUniformBuffers();
    createDescriptorPool();
    createRenderPass();
    createFramebuffer();
    createPipeline();
    loadModels();
    createSemaphores();
}