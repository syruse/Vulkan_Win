#include "VulkanRenderer.h"
#include "ObjModel.h"
#include "Particle.h"
#include "PipelineCreatorParticle.h"
#include "PipelineCreatorQuad.h"
#include "PipelineCreatorShadowMap.h"
#include "PipelineCreatorSkyBox.h"
#include "PipelineCreatorTextured.h"
#include "Skybox.h"
#include "Terrain.h"

#include <SDL.h>
#include <assert.h>
#include <algorithm>
#include <chrono>
#include <limits>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE  /// coerce the perspective projection matrix to be in depth: [0.0 to 1.0]
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

static constexpr float Z_NEAR = 0.01f;
static constexpr float Z_FAR = 1000.0f;

VulkanRenderer::VulkanRenderer(std::string_view appName, size_t width, size_t height)
    : VulkanState(appName, width, height),
      mTextureFactory(new TextureFactory(*this)),  /// this is not used imedially it's safe
      mCamera({65.0f, static_cast<float>(width) / height, Z_NEAR, Z_FAR}, {0.0f, 55.0f, -130.0f}) {
    assert(mTextureFactory);

    _pushConstant.windowSize = glm::vec4(_width, _height, Z_FAR, Z_NEAR);
    _pushConstant.lightPos = glm::vec3(500.0f, 500.0f, 0.0f);

    calculateLightThings();

    // TODO consider combining into one object with _pushConstant
    m_pushConstantRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_VERTEX_BIT;
    m_pushConstantRange.offset = 0;
    m_pushConstantRange.size = sizeof(PushConstant);

    assert(m_pipelineCreators.size() == Pipelines::MAX);
    m_pipelineCreators[TERRAIN].reset(new PipelineCreatorTextured(*this, m_renderPass, "vert_terrain.spv", "frag_terrain.spv"));
    m_pipelineCreators[GPASS].reset(new PipelineCreatorTextured(*this, m_renderPass, "vert_gPass.spv", "frag_gPass.spv"));
    m_pipelineCreators[SKYBOX].reset(
        new PipelineCreatorSkyBox(*this, m_renderPass, "vert_skybox.spv", "frag_skybox.spv", 0u, m_pushConstantRange));
    m_pipelineCreators[SHADOWMAP].reset(
        new PipelineCreatorShadowMap(*this, m_renderPassShadowMap, "vert_shadowMap.spv", "frag_shadowMap.spv"));
    m_pipelineCreators[POST_LIGHTING].reset(new PipelineCreatorQuad(
        *this, m_renderPass, "vert_gLigtingSubpass.spv", "frag_gLigtingSubpass.spv", true, true, 1u, m_pushConstantRange));
    m_pipelineCreators[POST_FXAA].reset(new PipelineCreatorQuad(*this, m_renderPassFXAA, "vert_fxaa.spv", "frag_fxaa.spv", false,
                                                                false, 0u, m_pushConstantRange));
    m_pipelineCreators[PARTICLE].reset(new PipelineCreatorParticle(*this, m_renderPassSemiTrans, "vert_particle.spv",
                                                                   "frag_particle.spv", 0u, m_pushConstantRange));
#ifndef NDEBUG
    for (auto i = 0u; i < Pipelines::MAX; ++i) {
        if (m_pipelineCreators[i] == nullptr) {
            Utils::printLog(ERROR_PARAM, "nullptr pipeline");
            return;
        }
    }
#endif

    m_models.emplace_back(new ObjModel(*this, *mTextureFactory, MODEL_PATH,
                                       static_cast<PipelineCreatorTextured*>(m_pipelineCreators[GPASS].get()), 10U));
    m_models.emplace_back(new Terrain(*this, *mTextureFactory, "noise.jpg", "grass1.jpg", "grass2.jpg",
                                      static_cast<PipelineCreatorTextured*>(m_pipelineCreators[TERRAIN].get()), Z_FAR));

    const std::array<std::string_view, 6> skyBoxTextures{"sky_ft.png", "sky_bk.png", "sky_dn.png",
                                                         "sky_up.png", "sky_lt.png", "sky_rt.png"};
    m_models.emplace_back(new Skybox(*this, *mTextureFactory, skyBoxTextures,
                                     static_cast<PipelineCreatorTextured*>(m_pipelineCreators[SKYBOX].get())));

    m_bush = std::make_unique<Particle>(*this, *mTextureFactory, "bush.png",
                                        static_cast<PipelineCreatorTextured*>(m_pipelineCreators[PARTICLE].get()), 10000u,
                                        0.7f * Z_FAR, 10);
}

VulkanRenderer::~VulkanRenderer() {
    Pipeliner::getInstance().saveCache();

    cleanupSwapChain();

    mTextureFactory.reset(nullptr);

    _aligned_free(mp_modelTransferSpace);

    for (size_t i = 0u; i < MAX_FRAMES_IN_FLIGHT; i++) {
        vkDestroyBuffer(_core.getDevice(), _ubo.buffers[i], nullptr);
        vkFreeMemory(_core.getDevice(), _ubo.buffersMemory[i], nullptr);
        vkDestroyBuffer(_core.getDevice(), _dynamicUbo.buffers[i], nullptr);
        vkFreeMemory(_core.getDevice(), _dynamicUbo.buffersMemory[i], nullptr);
    }

    for (size_t i = 0u; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        vkDestroySemaphore(_core.getDevice(), m_presentCompleteSem[i], nullptr);
        vkDestroySemaphore(_core.getDevice(), m_renderCompleteSem[i], nullptr);
        vkDestroyFence(_core.getDevice(), m_drawFences[i], nullptr);
    }

    vkDestroyCommandPool(_core.getDevice(), _cmdBufPool, nullptr);
}

void VulkanRenderer::cleanupSwapChain() {
    // Wait until no actions being run on device before destroying
    vkDeviceWaitIdle(_core.getDevice());

    vkFreeCommandBuffers(_core.getDevice(), _cmdBufPool, static_cast<uint32_t>(_cmdBufs.size()), _cmdBufs.data());

    vkDestroyImageView(_core.getDevice(), _depthBuffer.depthImageView, nullptr);
    vkDestroyImage(_core.getDevice(), _depthBuffer.depthImage, nullptr);
    vkFreeMemory(_core.getDevice(), _depthBuffer.depthImageMemory, nullptr);

    vkDestroyImageView(_core.getDevice(), _shadowMapBuffer.depthImageView, nullptr);
    vkDestroyImage(_core.getDevice(), _shadowMapBuffer.depthImage, nullptr);
    vkFreeMemory(_core.getDevice(), _shadowMapBuffer.depthImageMemory, nullptr);

    for (size_t i = 0u; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        vkDestroyImageView(_core.getDevice(), _colorBuffer.colorBufferImageView[i], nullptr);
        vkDestroyImage(_core.getDevice(), _colorBuffer.colorBufferImage[i], nullptr);
        vkFreeMemory(_core.getDevice(), _colorBuffer.colorBufferImageMemory[i], nullptr);

        vkDestroyImageView(_core.getDevice(), _gPassBuffer.normal.colorBufferImageView[i], nullptr);
        vkDestroyImage(_core.getDevice(), _gPassBuffer.normal.colorBufferImage[i], nullptr);
        vkFreeMemory(_core.getDevice(), _gPassBuffer.normal.colorBufferImageMemory[i], nullptr);

        vkDestroyImageView(_core.getDevice(), _gPassBuffer.color.colorBufferImageView[i], nullptr);
        vkDestroyImage(_core.getDevice(), _gPassBuffer.color.colorBufferImage[i], nullptr);
        vkFreeMemory(_core.getDevice(), _gPassBuffer.color.colorBufferImageMemory[i], nullptr);
    }

    for (auto& framebuffer : m_fbs) {
        vkDestroyFramebuffer(_core.getDevice(), framebuffer, nullptr);
    }
    for (auto& framebuffer : m_fbsFXAA) {
        vkDestroyFramebuffer(_core.getDevice(), framebuffer, nullptr);
    }
    for (auto& framebuffer : m_fbsSemiTrans) {
        vkDestroyFramebuffer(_core.getDevice(), framebuffer, nullptr);
    }
    for (auto& framebuffer : m_fbsShadowMap) {
        vkDestroyFramebuffer(_core.getDevice(), framebuffer, nullptr);
    }

    for (auto& imageView : _swapChain.views) {
        vkDestroyImageView(_core.getDevice(), imageView, nullptr);
    }

    vkDestroySwapchainKHR(_core.getDevice(), _swapChain.handle, nullptr);

    vkDestroyRenderPass(_core.getDevice(), m_renderPass, nullptr);
    vkDestroyRenderPass(_core.getDevice(), m_renderPassFXAA, nullptr);
    vkDestroyRenderPass(_core.getDevice(), m_renderPassShadowMap, nullptr);
    vkDestroyRenderPass(_core.getDevice(), m_renderPassSemiTrans, nullptr);

    for (auto& pipelineCreator : m_pipelineCreators) {
        pipelineCreator->destroyDescriptorPool();
    }
}

void VulkanRenderer::recreateSwapChain(uint16_t width, uint16_t height) {
    INFO_FORMAT(" new width=%d; new height=%d", width, height);
    if (_width != width || _height != height) {
        cleanupSwapChain();

        _width = width;
        _height = height;
        m_currentFrame = 0u;

        _pushConstant.windowSize = glm::vec4(_width, _height, Z_FAR, Z_NEAR);
        calculateLightThings();

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

void VulkanRenderer::recreateDescriptorSets() {
    for (auto& pipelineCreator : m_pipelineCreators) {
        pipelineCreator->recreateDescriptors();
    }
}

void VulkanRenderer::calculateLightThings() {
    float aspectRatio = static_cast<float>(_height) / _width;
    m_lightProj = glm::ortho(-Z_FAR, Z_FAR, -Z_FAR * aspectRatio, Z_FAR * aspectRatio, -Z_FAR, Z_FAR) *
                  glm::lookAt(_pushConstant.lightPos, glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    m_lightProj[1][1] *= -1;
}

void VulkanRenderer::updateUniformBuffer(uint32_t currentImage) {
    assert(_ubo.buffersMemory.size() > currentImage);

    const auto objectsAmount = m_models.size();

    const auto& cameraViewProj = mCamera.viewProjMat();
    const auto& model = mCamera.targetModelMat();

    static ViewProj viewProj{};
    viewProj.viewProj = cameraViewProj.proj * cameraViewProj.view;
    viewProj.viewProjInverse = glm::inverse(viewProj.viewProj);
    viewProj.lightViewProj = m_lightProj;

    // Copy VP data
    void* data;
    vkMapMemory(_core.getDevice(), _ubo.buffersMemory[currentImage], 0, sizeof(viewProj), 0, &data);
    memcpy(data, &viewProj, sizeof(viewProj));
    vkUnmapMemory(_core.getDevice(), _ubo.buffersMemory[currentImage]);

    // Copy Model data except skybox
    for (size_t i = 1u; i < objectsAmount - 1; i++) {
        Model* pModel = (Model*)((uint64_t)mp_modelTransferSpace + (i * _modelUniformAlignment));
        pModel->model = glm::mat4(1.0f);
        pModel->MVP = viewProj.viewProj * pModel->model;
    }
    // set target model matrix from Camera for our main 3d model
    Model* pModel = (Model*)((uint64_t)mp_modelTransferSpace);
    pModel->model = model;
    pModel->MVP = viewProj.viewProj * pModel->model;

    // move skybox to get unreachable
    pModel = (Model*)((uint64_t)mp_modelTransferSpace + (objectsAmount - 1) * _modelUniformAlignment);
    pModel->model = glm::translate(glm::mat4(1.0f), mCamera.targetPos());
    pModel->MVP = viewProj.viewProj * pModel->model;

    // Map the list of model data
    vkMapMemory(_core.getDevice(), _dynamicUbo.buffersMemory[currentImage], 0, _modelUniformAlignment * objectsAmount, 0, &data);
    memcpy(data, mp_modelTransferSpace, _modelUniformAlignment * objectsAmount);
    vkUnmapMemory(_core.getDevice(), _dynamicUbo.buffersMemory[currentImage]);
}

void VulkanRenderer::allocateDynamicBufferTransferSpace() {
    // Get properties of our new device
    VkPhysicalDeviceProperties deviceProperties;
    vkGetPhysicalDeviceProperties(_core.getPhysDevice(), &deviceProperties);

    size_t minUniformBufferOffset = static_cast<size_t>(deviceProperties.limits.minUniformBufferOffsetAlignment);

    // Calculate alignment of model matrix data
    _modelUniformAlignment = (sizeof(Model) + minUniformBufferOffset - 1) & ~(minUniformBufferOffset - 1);

    // Create space in memory to hold dynamic buffer that is aligned to our required alignment and holds m_models.size()
    mp_modelTransferSpace = (Model*)_aligned_malloc(_modelUniformAlignment * m_models.size(), _modelUniformAlignment);
}

void VulkanRenderer::createDescriptorPool() {
    for (auto& pipelineCreator : m_pipelineCreators) {
        pipelineCreator->createDescriptorPool();
    }
}

void VulkanRenderer::createSwapChain() {
    const VkSurfaceCapabilitiesKHR& SurfaceCaps = _core.getSurfaceCaps();
    static const VkPresentModeKHR presentMode = _core.getPresentMode();

    assert(SurfaceCaps.currentExtent.width != -1);

    // maxImageCount: value of 0 means that there is no limit on the number of images
    if (SurfaceCaps.maxImageCount) {
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
    SwapChainCreateInfo.presentMode = presentMode;
    SwapChainCreateInfo.clipped = VK_TRUE;
    SwapChainCreateInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;

    VkResult res = vkCreateSwapchainKHR(_core.getDevice(), &SwapChainCreateInfo, nullptr, &_swapChain.handle);
    CHECK_VULKAN_ERROR("vkCreateSwapchainKHR error %d\n", res);

    Utils::printLog(INFO_PARAM, "Swap chain created");

    uint32_t NumSwapChainImages = 0;
    res = vkGetSwapchainImagesKHR(_core.getDevice(), _swapChain.handle, &NumSwapChainImages, nullptr);
    CHECK_VULKAN_ERROR("vkGetSwapchainImagesKHR error %d\n", res);
    assert(MAX_FRAMES_IN_FLIGHT <= NumSwapChainImages);
    Utils::printLog(INFO_PARAM, "Available number of presentable images ", NumSwapChainImages);
    NumSwapChainImages = MAX_FRAMES_IN_FLIGHT;  // queried number of presentable images
    res = vkGetSwapchainImagesKHR(_core.getDevice(), _swapChain.handle, &NumSwapChainImages, &(_swapChain.images[0]));
    CHECK_VULKAN_ERROR("vkGetSwapchainImagesKHR error %d\n", res);
}

void VulkanRenderer::createUniformBuffers() {
    // ViewProjection buffer size
    VkDeviceSize bufferSize = sizeof(ViewProj);

    // Model buffer size
    VkDeviceSize modelBufferSize = _modelUniformAlignment * m_models.size();

    /**
     * We should have multiple buffers, because multiple frames may be in flight at the same time and
     * we don't want to update the buffer in preparation of the next frame while a previous one is still reading from it!
     */
    for (size_t i = 0; i < _swapChain.images.size(); i++) {
        Utils::VulkanCreateBuffer(_core.getDevice(), _core.getPhysDevice(), bufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, _ubo.buffers[i],
                                  _ubo.buffersMemory[i]);
        Utils::VulkanCreateBuffer(_core.getDevice(), _core.getPhysDevice(), modelBufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                  _dynamicUbo.buffers[i], _dynamicUbo.buffersMemory[i]);
    }
}

void VulkanRenderer::createCommandPool() {
    VkCommandPoolCreateInfo cmdPoolCreateInfo = {};
    cmdPoolCreateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    cmdPoolCreateInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;  /// comand buffer will be reset when VkBegin
                                                                                /// called, so it's resetable now
    cmdPoolCreateInfo.queueFamilyIndex = _core.getQueueFamily();

    VkResult res = vkCreateCommandPool(_core.getDevice(), &cmdPoolCreateInfo, nullptr, &_cmdBufPool);
    CHECK_VULKAN_ERROR("vkCreateCommandPool error %d\n", res);

    Utils::printLog(INFO_PARAM, "Command buffer pool created");
}

void VulkanRenderer::createCommandBuffer() {
    VkCommandBufferAllocateInfo cmdBufAllocInfo = {};
    cmdBufAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmdBufAllocInfo.commandPool = _cmdBufPool;
    cmdBufAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmdBufAllocInfo.commandBufferCount = _swapChain.images.size();

    VkResult res = vkAllocateCommandBuffers(_core.getDevice(), &cmdBufAllocInfo, &_cmdBufs[0]);
    CHECK_VULKAN_ERROR("vkAllocateCommandBuffers error %d\n", res);

    Utils::printLog(INFO_PARAM, "Created command buffers");
}

void VulkanRenderer::recordCommandBuffers(uint32_t currentImage) {
    VkCommandBufferBeginInfo beginInfo = {};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;

    VkResult res = vkBeginCommandBuffer(_cmdBufs[currentImage], &beginInfo);
    CHECK_VULKAN_ERROR("vkBeginCommandBuffer error %d\n", res);

    //---------------------------------------------------------------------------------------------//
    /// shadow map pass
    VkClearValue shadowMapClearValues{};
    shadowMapClearValues.depthStencil.depth = 1.0f;

    VkRenderPassBeginInfo renderPassShadowMapInfo = {};
    renderPassShadowMapInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassShadowMapInfo.renderPass = m_renderPassShadowMap;
    renderPassShadowMapInfo.renderArea.offset.x = 0;
    renderPassShadowMapInfo.renderArea.offset.y = 0;
    renderPassShadowMapInfo.renderArea.extent.width = _width;
    renderPassShadowMapInfo.renderArea.extent.height = _height;
    renderPassShadowMapInfo.clearValueCount = 1;
    renderPassShadowMapInfo.pClearValues = &shadowMapClearValues;
    renderPassShadowMapInfo.framebuffer = m_fbsShadowMap[currentImage];

    vkCmdBeginRenderPass(_cmdBufs[currentImage], &renderPassShadowMapInfo, VK_SUBPASS_CONTENTS_INLINE);

    // draw shadow of each object
    for (uint32_t meshIndex = 0u; meshIndex < m_models.size(); ++meshIndex) {
        const uint32_t dynamicOffset = static_cast<uint32_t>(_modelUniformAlignment) * meshIndex;
        m_models[meshIndex]->drawWithCustomPipeline(m_pipelineCreators[SHADOWMAP].get(), _cmdBufs[currentImage], currentImage,
                                                    dynamicOffset);
    }

    vkCmdEndRenderPass(_cmdBufs[currentImage]);

    //---------------------------------------------------------------------------------------------//
    /// G pass
    VkClearValue clearValue;
    clearValue.color = {0.0f, 0.0f, 0.0f, 1.0f};
    std::vector<VkClearValue> clearValues(5, clearValue);
    clearValues[3] = VkClearValue{};  // TODO make it flexible
    clearValues[3].depthStencil.depth = 1.0f;
    clearValues[4] = clearValues[3];

    VkRenderPassBeginInfo renderPassInfo = {};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = m_renderPass;
    renderPassInfo.renderArea.offset.x = 0;
    renderPassInfo.renderArea.offset.y = 0;
    renderPassInfo.renderArea.extent.width = _width;
    renderPassInfo.renderArea.extent.height = _height;
    renderPassInfo.clearValueCount = clearValues.size();
    renderPassInfo.pClearValues = clearValues.data();
    renderPassInfo.framebuffer = m_fbs[currentImage];

    Utils::VulkanImageMemoryBarrier(_cmdBufs[currentImage], _colorBuffer.colorBufferImage[currentImage], _colorBuffer.colorFormat,
                                    VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                    VK_IMAGE_ASPECT_COLOR_BIT, 1U, 1U, 0, 0, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                                    VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT);

    vkCmdBeginRenderPass(_cmdBufs[currentImage], &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

    ///  SkyBox and 3D Models
    for (uint32_t meshIndex = 0u; meshIndex < m_models.size(); ++meshIndex) {
        const uint32_t dynamicOffset = static_cast<uint32_t>(_modelUniformAlignment) * meshIndex;
        m_models[meshIndex]->draw(_cmdBufs[currentImage], currentImage, dynamicOffset);
    }

    /// For Each mesh end

    ///-----------------------------------------------------------------------------------///
    /// Start second subpass
    vkCmdNextSubpass(_cmdBufs[currentImage], VK_SUBPASS_CONTENTS_INLINE);

    /// quad subpass
    {
        const auto& pipelineCreator = m_pipelineCreators[POST_LIGHTING];
        vkCmdPushConstants(_cmdBufs[currentImage], pipelineCreator->getPipeline()->pipelineLayout,
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushConstant), &_pushConstant);

        vkCmdBindPipeline(_cmdBufs[currentImage], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineCreator->getPipeline()->pipeline);
        vkCmdBindDescriptorSets(_cmdBufs[currentImage], VK_PIPELINE_BIND_POINT_GRAPHICS,
                                pipelineCreator->getPipeline()->pipelineLayout, 0, 1,
                                pipelineCreator->getDescriptorSet(currentImage), 0, nullptr);
    }

    vkCmdDraw(_cmdBufs[currentImage], 6, 1, 0, 0);

    vkCmdEndRenderPass(_cmdBufs[currentImage]);

    //---------------------------------------------------------------------------------------------//
    /// SEMI-TRANSPARENT OBJECTS render pass

    std::array<VkClearValue, 2> semiTransClearValues{};
    semiTransClearValues[0].color = {0.0f, 0.0f, 0.0f, 0.0f};
    semiTransClearValues[1].color = {0.0f, 0.0f, 0.0f, 0.0f};

    VkRenderPassBeginInfo renderPassSemiTransInfo = {};
    renderPassSemiTransInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassSemiTransInfo.renderPass = m_renderPassSemiTrans;
    renderPassSemiTransInfo.renderArea.offset.x = 0;
    renderPassSemiTransInfo.renderArea.offset.y = 0;
    renderPassSemiTransInfo.renderArea.extent.width = _width;
    renderPassSemiTransInfo.renderArea.extent.height = _height;
    renderPassSemiTransInfo.clearValueCount = semiTransClearValues.size();
    renderPassSemiTransInfo.pClearValues = semiTransClearValues.data();
    renderPassSemiTransInfo.framebuffer = m_fbsSemiTrans[currentImage];

    Utils::VulkanImageMemoryBarrier(_cmdBufs[currentImage], _colorBuffer.colorBufferImage[currentImage], _colorBuffer.colorFormat,
                                    VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                    VK_IMAGE_ASPECT_COLOR_BIT, 1U, 1U, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                                    VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                                    VK_PIPELINE_STAGE_VERTEX_SHADER_BIT);

    vkCmdBeginRenderPass(_cmdBufs[currentImage], &renderPassSemiTransInfo, VK_SUBPASS_CONTENTS_INLINE);
    {
        const auto& pipelineCreator = m_pipelineCreators[PARTICLE];
        vkCmdPushConstants(_cmdBufs[currentImage], pipelineCreator->getPipeline()->pipelineLayout,
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushConstant), &_pushConstant);
        m_bush->draw(_cmdBufs[currentImage], currentImage);
    }
    vkCmdEndRenderPass(_cmdBufs[currentImage]);

    //---------------------------------------------------------------------------------------------//
    /// FXAA render pass

    std::array<VkClearValue, 2> fxaaClearValues{};
    fxaaClearValues[0].color = {0.0f, 0.0f, 0.0f, 1.0f};
    fxaaClearValues[1].color = {0.0f, 0.0f, 0.0f, 1.0f};

    VkRenderPassBeginInfo renderPassFXAAInfo = {};
    renderPassFXAAInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassFXAAInfo.renderPass = m_renderPassFXAA;
    renderPassFXAAInfo.renderArea.offset.x = 0;
    renderPassFXAAInfo.renderArea.offset.y = 0;
    renderPassFXAAInfo.renderArea.extent.width = _width;
    renderPassFXAAInfo.renderArea.extent.height = _height;
    renderPassFXAAInfo.clearValueCount = fxaaClearValues.size();
    renderPassFXAAInfo.pClearValues = fxaaClearValues.data();
    renderPassFXAAInfo.framebuffer = m_fbsFXAA[currentImage];

    Utils::VulkanImageMemoryBarrier(_cmdBufs[currentImage], _colorBuffer.colorBufferImage[currentImage], _colorBuffer.colorFormat,
                                    VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                    VK_IMAGE_ASPECT_COLOR_BIT, 1U, 1U, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                                    VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                                    VK_PIPELINE_STAGE_VERTEX_SHADER_BIT);

    vkCmdBeginRenderPass(_cmdBufs[currentImage], &renderPassFXAAInfo, VK_SUBPASS_CONTENTS_INLINE);
    {
        const auto& pipelineCreator = m_pipelineCreators[POST_FXAA];
        vkCmdPushConstants(_cmdBufs[currentImage], pipelineCreator->getPipeline()->pipelineLayout,
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushConstant), &_pushConstant);
        vkCmdBindPipeline(_cmdBufs[currentImage], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineCreator->getPipeline()->pipeline);
        vkCmdBindDescriptorSets(_cmdBufs[currentImage], VK_PIPELINE_BIND_POINT_GRAPHICS,
                                pipelineCreator->getPipeline()->pipelineLayout, 0, 1,
                                pipelineCreator->getDescriptorSet(currentImage), 0, nullptr);
    }

    vkCmdDraw(_cmdBufs[currentImage], 6, 1, 0, 0);

    vkCmdEndRenderPass(_cmdBufs[currentImage]);

    Utils::VulkanImageMemoryBarrier(_cmdBufs[currentImage], _colorBuffer.colorBufferImage[currentImage], _colorBuffer.colorFormat,
                                    VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_IMAGE_ASPECT_COLOR_BIT, 1U, 1U,
                                    VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, 0, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                                    VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);

    res = vkEndCommandBuffer(_cmdBufs[currentImage]);
    CHECK_VULKAN_ERROR("vkEndCommandBuffer error %d\n", res);
}

void VulkanRenderer::createColorBufferImage() {
    // Get supported format for color attachment

    if (!Utils::VulkanFindSupportedFormat(_core.getPhysDevice(), {VK_FORMAT_R8G8B8A8_UNORM}, VK_IMAGE_TILING_OPTIMAL,
                                          VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT, _colorBuffer.colorFormat)) {
        Utils::printLog(ERROR_PARAM, "failed to find supported format!");
    }

    if (!Utils::VulkanFindSupportedFormat(_core.getPhysDevice(), {VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_R16G16B16A16_SFLOAT},
                                          VK_IMAGE_TILING_OPTIMAL, VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT,
                                          _gPassBuffer.normal.colorFormat)) {
        Utils::printLog(ERROR_PARAM, "failed to find supported format!");
    }

    if (!Utils::VulkanFindSupportedFormat(_core.getPhysDevice(), {VK_FORMAT_R8G8B8A8_UNORM}, VK_IMAGE_TILING_OPTIMAL,
                                          VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT, _gPassBuffer.color.colorFormat)) {
        Utils::printLog(ERROR_PARAM, "failed to find supported format!");
    }

    for (size_t i = 0; i < _swapChain.images.size(); ++i) {
        // By keeping G Pass buffers on-tile only (VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT), we can save a lot of bandwidth and
        // memory. we don't need to write the g-buffer data out to memory let's leave everything in tile memory,
        // do the lighting pass within the tile (you read them as input attachments), and then forget them
        // In this sample, only _colorBuffer needs to be written out to memory and be used out of subpasses

        // Create Color Buffer Image
        Utils::VulkanCreateImage(
            _core.getDevice(), _core.getPhysDevice(), _width, _height, _colorBuffer.colorFormat, VK_IMAGE_TILING_OPTIMAL,
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            _colorBuffer.colorBufferImage[i], _colorBuffer.colorBufferImageMemory[i]);

        // Create Color Buffer Image View
        Utils::VulkanCreateImageView(_core.getDevice(), _colorBuffer.colorBufferImage[i], _colorBuffer.colorFormat,
                                     VK_IMAGE_ASPECT_COLOR_BIT, _colorBuffer.colorBufferImageView[i]);

        // the same applied to G pass buffer

        Utils::VulkanCreateImage(
            _core.getDevice(), _core.getPhysDevice(), _width, _height, _gPassBuffer.normal.colorFormat, VK_IMAGE_TILING_OPTIMAL,
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, _gPassBuffer.normal.colorBufferImage[i],
            _gPassBuffer.normal.colorBufferImageMemory[i]);

        Utils::VulkanCreateImageView(_core.getDevice(), _gPassBuffer.normal.colorBufferImage[i], _gPassBuffer.normal.colorFormat,
                                     VK_IMAGE_ASPECT_COLOR_BIT, _gPassBuffer.normal.colorBufferImageView[i]);

        Utils::VulkanCreateImage(
            _core.getDevice(), _core.getPhysDevice(), _width, _height, _gPassBuffer.color.colorFormat, VK_IMAGE_TILING_OPTIMAL,
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, _gPassBuffer.color.colorBufferImage[i],
            _gPassBuffer.color.colorBufferImageMemory[i]);

        Utils::VulkanCreateImageView(_core.getDevice(), _gPassBuffer.color.colorBufferImage[i], _gPassBuffer.color.colorFormat,
                                     VK_IMAGE_ASPECT_COLOR_BIT, _gPassBuffer.color.colorBufferImageView[i]);
    }
}

void VulkanRenderer::loadModels() {
    /// lazy init when core is ready
    mTextureFactory->init();

    for (auto& model : m_models) {
        model->init();
    }

    m_bush->init();

    // for first pair the calling can be skipped since it's already called in model->init()
    for (auto& pipelineCreator : m_pipelineCreators) {
        pipelineCreator->recreateDescriptors();
    }
}

bool VulkanRenderer::renderScene() {
    bool ret_status = true;

    const auto& winController = _core.getWinController();
    assert(winController);

    static auto startTime = std::chrono::high_resolution_clock::now();
    auto currentTime = std::chrono::high_resolution_clock::now();
    float deltaTime = std::chrono::duration<float, std::chrono::milliseconds::period>(currentTime - startTime).count();

    mCamera.update(deltaTime);

    auto windowQueueMSG = winController->processWindowQueueMSGs();  /// falls into NRVO
    ret_status = !windowQueueMSG.isQuited;

    if (windowQueueMSG.isResized) {
        mCamera.resetPerspective({65.0f, (float)windowQueueMSG.width / windowQueueMSG.height, 0.01f, 1000.0f});
        recreateSwapChain(windowQueueMSG.width, windowQueueMSG.height);
        return ret_status;
    }

    SDL_Event e;
    if (SDL_PollEvent(&e) != 0) {
        switch (e.type) {
            case SDL_QUIT: {
                SDL_Quit();
                ret_status = false;
                break;
            }
            case SDL_KEYDOWN: {
                if (e.key.keysym.sym == SDLK_w) {
                    mCamera.move(Camera::EDirection::Forward);
                } else if (e.key.keysym.sym == SDLK_a) {
                    mCamera.move(Camera::EDirection::Left);
                } else if (e.key.keysym.sym == SDLK_d) {
                    mCamera.move(Camera::EDirection::Right);
                } else if (e.key.keysym.sym == SDLK_s) {
                    mCamera.move(Camera::EDirection::Back);
                }
                break;
            }
            case SDL_MOUSEMOTION: {
                std::cout << "Motion " << e.motion.xrel;
                std::cout << "Motion " << e.motion.yrel;
                break;
            }
        };
    }

    _pushConstant.cameraPos = mCamera.cameraPosition();

    // -- GET NEXT IMAGE --
    // Wait for given fence to signal (open) from last draw before continuing
    vkWaitForFences(_core.getDevice(), 1, &m_drawFences[m_currentFrame], VK_TRUE, UINT64_MAX);
    // Manually reset (close) fences
    vkResetFences(_core.getDevice(), 1, &m_drawFences[m_currentFrame]);

    uint32_t ImageIndex = 0;
    VkResult res = vkAcquireNextImageKHR(_core.getDevice(), _swapChain.handle, UINT64_MAX, m_presentCompleteSem[m_currentFrame],
                                         VK_NULL_HANDLE, &ImageIndex);
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

    recordCommandBuffers(ImageIndex);  /// added here since now comand buffer resets after each vkBegin command
    updateUniformBuffer(ImageIndex);

    res = vkQueueSubmit(_queue, 1, &submitInfo, m_drawFences[m_currentFrame]);
    CHECK_VULKAN_ERROR("vkQueueSubmit error %d\n", res);

    VkPresentInfoKHR presentInfo = {};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &_swapChain.handle;
    presentInfo.pImageIndices = &ImageIndex;
    presentInfo.pWaitSemaphores = &m_renderCompleteSem[m_currentFrame];
    presentInfo.waitSemaphoreCount = 1;

    res = vkQueuePresentKHR(_queue, &presentInfo);
    if (res == VK_ERROR_OUT_OF_DATE_KHR) {
        recreateSwapChain(_width, _height);
    } else {
        CHECK_VULKAN_ERROR("vkQueuePresentKHR error %d\n", res);
    }

    // Get next frame (use % MAX_FRAME_DRAWS to keep value below MAX_FRAME_DRAWS)
    m_currentFrame = ++m_currentFrame % MAX_FRAMES_IN_FLIGHT;

    startTime = std::chrono::high_resolution_clock::now();

    return ret_status;
}
// TO FIX common code getting into functions
void VulkanRenderer::createRenderPass() {
    // Array of our subpasses
    std::array<VkSubpassDescription, 2> subpasses{};

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

    VkAttachmentDescription gPassNormalAttachment = colorAttachment;
    gPassNormalAttachment.format = _gPassBuffer.normal.colorFormat;
    gPassNormalAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;  // not needed after subpasses completed, for performance

    VkAttachmentDescription gPassColorAttachment = gPassNormalAttachment;
    gPassColorAttachment.format = _gPassBuffer.color.colorFormat;

    VkAttachmentDescription depthAttachment{};
    depthAttachment.format = _depthBuffer.depthFormat;
    depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depthAttachment.stencilLoadOp =
        VK_ATTACHMENT_LOAD_OP_DONT_CARE;  // for more efficiency and since it will not be used after drawing has finished
    depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;

    VkAttachmentDescription shadowMapAttachment = depthAttachment;

    VkAttachmentReference colorAttachmentRef{};
    colorAttachmentRef.attachment = 0;
    colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference gPassNormalAttachmentRef = colorAttachmentRef;
    gPassNormalAttachmentRef.attachment = 1;

    VkAttachmentReference gPassColorAttachmentRef = colorAttachmentRef;
    gPassColorAttachmentRef.attachment = 2;

    VkAttachmentReference depthAttachmentRef{};
    depthAttachmentRef.attachment = 3;
    depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;

    std::array<VkAttachmentReference, 3> gPassAttachment{colorAttachmentRef, gPassNormalAttachmentRef, gPassColorAttachmentRef};

    subpasses[0].pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpasses[0].colorAttachmentCount = gPassAttachment.size();
    subpasses[0].pColorAttachments = &gPassAttachment[0];
    subpasses[0].pDepthStencilAttachment = &depthAttachmentRef;

    // References to attachments that subpass will take input from
    std::array<VkAttachmentReference, 4> inputReferences;
    inputReferences[0].attachment = 1;
    inputReferences[0].layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    inputReferences[1].attachment = 2;
    inputReferences[1].layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    inputReferences[2].attachment = 3;
    inputReferences[2].layout = VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL;
    inputReferences[3].attachment = 4;
    inputReferences[3].layout = VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL;

    // Set up Subpass 2
    subpasses[1].pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpasses[1].colorAttachmentCount = 1;
    subpasses[1].pColorAttachments = &colorAttachmentRef;
    subpasses[1].inputAttachmentCount = static_cast<uint32_t>(inputReferences.size());
    subpasses[1].pInputAttachments = inputReferences.data();

    // SUBPASS DEPENDENCIES

    // Need to determine when layout transitions occur using subpass dependencies
    std::array<VkSubpassDependency, 3> subpassDependencies;

    // Conversion from VK_IMAGE_LAYOUT_UNDEFINED to VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
    // Transition must happen after...
    subpassDependencies[0].srcSubpass =
        VK_SUBPASS_EXTERNAL;  // Subpass index (VK_SUBPASS_EXTERNAL = Special value meaning outside of renderpass)
    subpassDependencies[0].srcStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;  // Pipeline initial stage
    subpassDependencies[0].srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;            // Stage access mask (memory access)
    // But must happen before...
    subpassDependencies[0].dstSubpass = 0;
    subpassDependencies[0].dstStageMask =
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    subpassDependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                                           VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
                                           VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
    subpassDependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

    // Subpass 1 layout (color/depth) to Subpass 2 layout (shader read)
    subpassDependencies[1].srcSubpass = 0;
    subpassDependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    subpassDependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                                           VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
                                           VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
    subpassDependencies[1].dstSubpass = 1;
    subpassDependencies[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    subpassDependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    subpassDependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

    // Conversion from VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL to VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
    // Transition must happen after...
    subpassDependencies[2].srcSubpass = 0;
    subpassDependencies[2].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    subpassDependencies[2].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    // But must happen before...
    subpassDependencies[2].dstSubpass = VK_SUBPASS_EXTERNAL;
    subpassDependencies[2].dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    subpassDependencies[2].dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
    subpassDependencies[2].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

    std::array<VkAttachmentDescription, 5> renderPassAttachments = {colorAttachment, gPassNormalAttachment, gPassColorAttachment,
                                                                    depthAttachment, shadowMapAttachment};

    // Create info for Render Pass
    VkRenderPassCreateInfo renderPassCreateInfo = {};
    renderPassCreateInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassCreateInfo.attachmentCount = static_cast<uint32_t>(renderPassAttachments.size());
    renderPassCreateInfo.pAttachments = renderPassAttachments.data();
    renderPassCreateInfo.subpassCount = static_cast<uint32_t>(subpasses.size());
    renderPassCreateInfo.pSubpasses = subpasses.data();
    renderPassCreateInfo.dependencyCount = static_cast<uint32_t>(subpassDependencies.size());
    renderPassCreateInfo.pDependencies = subpassDependencies.data();

    VkResult res = vkCreateRenderPass(_core.getDevice(), &renderPassCreateInfo, nullptr, &m_renderPass);
    CHECK_VULKAN_ERROR("vkCreateRenderPass error %d\n", res);

    Utils::printLog(INFO_PARAM, "Created a render pass G-PASS");

    //-------------------------------------------------------//
    // Create info for Render Pass for Semi Transparent particles
    VkAttachmentDescription colorAttachmentSemiTrans = colorAttachment;
    colorAttachmentSemiTrans.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    colorAttachmentSemiTrans.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachmentSemiTrans.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentDescription depthAttachmentSemiTrans = depthAttachment;
    depthAttachmentSemiTrans.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    depthAttachmentSemiTrans.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachmentSemiTrans.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthAttachmentSemiTrans.finalLayout = VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL;

    VkAttachmentReference colorAttachmentSemiTransReference = {};
    colorAttachmentSemiTransReference.attachment = 0;
    colorAttachmentSemiTransReference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depthAttachmentSemiTransReference;
    depthAttachmentSemiTransReference.attachment = 1;
    depthAttachmentSemiTransReference.layout = VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL;

    VkSubpassDescription subpassSemiTrans{};
    subpassSemiTrans.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpassSemiTrans.colorAttachmentCount = 1;
    subpassSemiTrans.pColorAttachments = &colorAttachmentSemiTransReference;
    subpassSemiTrans.inputAttachmentCount = 1;
    subpassSemiTrans.pInputAttachments = &depthAttachmentSemiTransReference;
    // subpassSemiTrans.pDepthStencilAttachment = &depthAttachmentSemiTransReference;

    std::array<VkAttachmentDescription, 2> renderPassAttachmentsSemiTrans = {colorAttachmentSemiTrans, depthAttachmentSemiTrans};

    VkSubpassDependency dependencySemiTrans;

    dependencySemiTrans.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependencySemiTrans.dstSubpass = 0;
    dependencySemiTrans.srcStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    dependencySemiTrans.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependencySemiTrans.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
    dependencySemiTrans.dstAccessMask =
        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dependencySemiTrans.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

    VkRenderPassCreateInfo renderPassCreateInfoSemiTrans = {};
    renderPassCreateInfoSemiTrans.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassCreateInfoSemiTrans.attachmentCount = static_cast<uint32_t>(renderPassAttachmentsSemiTrans.size());
    renderPassCreateInfoSemiTrans.pAttachments = renderPassAttachmentsSemiTrans.data();
    renderPassCreateInfoSemiTrans.subpassCount = 1;
    renderPassCreateInfoSemiTrans.pSubpasses = &subpassSemiTrans;
    renderPassCreateInfoSemiTrans.dependencyCount = 1;
    renderPassCreateInfoSemiTrans.pDependencies = &dependencySemiTrans;

    res = vkCreateRenderPass(_core.getDevice(), &renderPassCreateInfoSemiTrans, nullptr, &m_renderPassSemiTrans);
    CHECK_VULKAN_ERROR("vkCreateRenderPass error %d\n", res);

    Utils::printLog(INFO_PARAM, "Created a render pass for SEMI-TRANSPARENT OBJECTS");

    //-------------------------------------------------------//
    // Create info for Render Pass FXAA
    VkAttachmentDescription colorAttachmentFXAA = {};
    colorAttachmentFXAA.format = _core.getSurfaceFormat().format;         // Format to use for attachment
    colorAttachmentFXAA.samples = VK_SAMPLE_COUNT_1_BIT;                  // Number of samples to write for multisampling
    colorAttachmentFXAA.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;              // Describes what to do with attachment before rendering
    colorAttachmentFXAA.storeOp = VK_ATTACHMENT_STORE_OP_STORE;           // Describes what to do with attachment after rendering
    colorAttachmentFXAA.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;  // Describes what to do with stencil before rendering
    colorAttachmentFXAA.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;  // Describes what to do with stencil after rendering

    // Framebuffer data will be stored as an image, but images can be given different data layouts
    // to give optimal use for certain operations
    colorAttachmentFXAA.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;      // Image data layout before render pass starts
    colorAttachmentFXAA.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;  // Image data layout after render pass (to change to)

    // Attachment reference uses an attachment index that refers to index in the attachment list passed to renderPassCreateInfo
    VkAttachmentReference swapchainColorAttachmentReferenceFXAA = {};
    swapchainColorAttachmentReferenceFXAA.attachment = 0;
    swapchainColorAttachmentReferenceFXAA.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    // References to attachments that subpass will take input from
    VkAttachmentReference inputReferenceFXAA;
    inputReferenceFXAA.attachment = 1;
    inputReferenceFXAA.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkSubpassDescription subpassFXAA{};
    subpassFXAA.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpassFXAA.colorAttachmentCount = 1;
    subpassFXAA.pColorAttachments = &swapchainColorAttachmentReferenceFXAA;
    subpassFXAA.inputAttachmentCount = 1;
    subpassFXAA.pInputAttachments = &inputReferenceFXAA;

    std::array<VkAttachmentDescription, 2> renderPassAttachmentsFXAA = {colorAttachmentFXAA, colorAttachmentFXAA};

    // Subpass dependencies for layout transitions
    VkSubpassDependency dependencyFXAA;

    dependencyFXAA.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependencyFXAA.dstSubpass = 0;
    dependencyFXAA.srcStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    dependencyFXAA.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependencyFXAA.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
    dependencyFXAA.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dependencyFXAA.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

    VkRenderPassCreateInfo renderPassCreateInfoFXAA = {};
    renderPassCreateInfoFXAA.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassCreateInfoFXAA.attachmentCount = static_cast<uint32_t>(renderPassAttachmentsFXAA.size());
    renderPassCreateInfoFXAA.pAttachments = renderPassAttachmentsFXAA.data();
    renderPassCreateInfoFXAA.subpassCount = 1;
    renderPassCreateInfoFXAA.pSubpasses = &subpassFXAA;
    renderPassCreateInfoFXAA.dependencyCount = 1;
    renderPassCreateInfoFXAA.pDependencies = &dependencyFXAA;

    res = vkCreateRenderPass(_core.getDevice(), &renderPassCreateInfoFXAA, nullptr, &m_renderPassFXAA);
    CHECK_VULKAN_ERROR("vkCreateRenderPass error %d\n", res);

    Utils::printLog(INFO_PARAM, "Created a render pass FXAA");

    //-------------------------------------------------------//
    // Create info for Render Pass SHADOW MAP

    VkAttachmentReference depthAttachmentShadowMapRef{};
    depthAttachmentShadowMapRef.attachment = 0;
    depthAttachmentShadowMapRef.layout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpassesShadowMap{};
    subpassesShadowMap.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpassesShadowMap.colorAttachmentCount = 0;
    subpassesShadowMap.pDepthStencilAttachment = &depthAttachmentShadowMapRef;

    // Subpass dependencies for layout transitions
    std::array<VkSubpassDependency, 2> dependencies;

    dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[0].dstSubpass = 0;
    dependencies[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dependencies[0].dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependencies[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    dependencies[0].dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

    dependencies[1].srcSubpass = 0;
    dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[1].srcStageMask = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    dependencies[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dependencies[1].srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

    VkRenderPassCreateInfo renderPassCreateInfoShadowMap = {};
    renderPassCreateInfoShadowMap.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassCreateInfoShadowMap.attachmentCount = 1;
    renderPassCreateInfoShadowMap.pAttachments = &shadowMapAttachment;
    renderPassCreateInfoShadowMap.subpassCount = 1;
    renderPassCreateInfoShadowMap.pSubpasses = &subpassesShadowMap;
    renderPassCreateInfoShadowMap.dependencyCount = dependencies.size();
    renderPassCreateInfoShadowMap.pDependencies = dependencies.data();

    res = vkCreateRenderPass(_core.getDevice(), &renderPassCreateInfoShadowMap, nullptr, &m_renderPassShadowMap);
    CHECK_VULKAN_ERROR("vkCreateRenderPass error %d\n", res);

    Utils::printLog(INFO_PARAM, "Created a render pass SHADOW MAP");
}

void VulkanRenderer::createFramebuffer() {
    VkResult res;
    for (size_t i = 0; i < _swapChain.images.size(); i++) {
        std::array<VkImageView, 5> attachments = {
            _colorBuffer.colorBufferImageView[i], _gPassBuffer.normal.colorBufferImageView[i],
            _gPassBuffer.color.colorBufferImageView[i], _depthBuffer.depthImageView, _shadowMapBuffer.depthImageView};

        // The color attachment differs for every swap chain image,
        // but the same depth image can be used by all of them
        // because only a single subpass is running at the same time due to our semaphores.

        VkFramebufferCreateInfo fbCreateInfo = {};
        fbCreateInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbCreateInfo.renderPass = m_renderPass;
        fbCreateInfo.attachmentCount = attachments.size();
        fbCreateInfo.pAttachments = attachments.data();
        fbCreateInfo.width = _width;
        fbCreateInfo.height = _height;
        fbCreateInfo.layers = 1;

        res = vkCreateFramebuffer(_core.getDevice(), &fbCreateInfo, nullptr, &m_fbs[i]);
        CHECK_VULKAN_ERROR("vkCreateFramebuffer error %d\n", res);
    }

    //-------------------------------------------------------//
    // FBO FXAA
    for (size_t i = 0; i < _swapChain.images.size(); i++) {
        if (Utils::VulkanCreateImageView(_core.getDevice(), _swapChain.images[i], _core.getSurfaceFormat().format,
                                         VK_IMAGE_ASPECT_COLOR_BIT, _swapChain.views[i]) != VK_SUCCESS) {
            Utils::printLog(ERROR_PARAM, "failed to create texture image view!");
        }

        std::array<VkImageView, 2> attachments = {_swapChain.views[i], _colorBuffer.colorBufferImageView[i]};

        VkFramebufferCreateInfo fbCreateInfo = {};
        fbCreateInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbCreateInfo.renderPass = m_renderPassFXAA;
        fbCreateInfo.attachmentCount = attachments.size();
        fbCreateInfo.pAttachments = attachments.data();
        fbCreateInfo.width = _width;
        fbCreateInfo.height = _height;
        fbCreateInfo.layers = 1;

        res = vkCreateFramebuffer(_core.getDevice(), &fbCreateInfo, nullptr, &m_fbsFXAA[i]);
        CHECK_VULKAN_ERROR("vkCreateFramebuffer FXAA error %d\n", res);
    }

    //-------------------------------------------------------//
    // FBO SEMI-TRANSPARENT OBJECTS
    for (size_t i = 0; i < _swapChain.images.size(); i++) {
        std::array<VkImageView, 2> attachments = {_colorBuffer.colorBufferImageView[i], _depthBuffer.depthImageView};

        VkFramebufferCreateInfo fbCreateInfo = {};
        fbCreateInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbCreateInfo.renderPass = m_renderPassSemiTrans;
        fbCreateInfo.attachmentCount = attachments.size();
        fbCreateInfo.pAttachments = attachments.data();
        fbCreateInfo.width = _width;
        fbCreateInfo.height = _height;
        fbCreateInfo.layers = 1;

        res = vkCreateFramebuffer(_core.getDevice(), &fbCreateInfo, nullptr, &m_fbsSemiTrans[i]);
        CHECK_VULKAN_ERROR("vkCreateFramebuffer SEMI-TRANSPARENT OBJECTS error %d\n", res);
    }

    //-------------------------------------------------------//
    // FBO SHADOW MAP
    for (size_t i = 0; i < _swapChain.images.size(); i++) {
        VkImageView attachment = _shadowMapBuffer.depthImageView;

        VkFramebufferCreateInfo fbCreateInfo = {};
        fbCreateInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbCreateInfo.renderPass = m_renderPassShadowMap;
        fbCreateInfo.attachmentCount = 1;
        fbCreateInfo.pAttachments = &attachment;
        fbCreateInfo.width = _width;
        fbCreateInfo.height = _height;
        fbCreateInfo.layers = 1;

        res = vkCreateFramebuffer(_core.getDevice(), &fbCreateInfo, nullptr, &m_fbsShadowMap[i]);
        CHECK_VULKAN_ERROR("vkCreateFramebuffer SHADOW MAP error %d\n", res);
    }

    Utils::printLog(INFO_PARAM, "Frame buffers created");
}

void VulkanRenderer::createSemaphores() {
    // Semaphore creation information
    VkSemaphoreCreateInfo semaphoreCreateInfo = {};
    semaphoreCreateInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    // Fence creation information
    VkFenceCreateInfo fenceCreateInfo = {};
    fenceCreateInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceCreateInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        if (vkCreateSemaphore(_core.getDevice(), &semaphoreCreateInfo, nullptr, &m_presentCompleteSem[i]) != VK_SUCCESS ||
            vkCreateSemaphore(_core.getDevice(), &semaphoreCreateInfo, nullptr, &m_renderCompleteSem[i]) != VK_SUCCESS ||
            vkCreateFence(_core.getDevice(), &fenceCreateInfo, nullptr, &m_drawFences[i]) != VK_SUCCESS) {
            Utils::printLog(ERROR_PARAM, "Failed to create a Semaphore and/or Fence!");
        }
    }
}

void VulkanRenderer::createPipeline() {
    for (auto& pipelineCreator : m_pipelineCreators) {
        pipelineCreator->recreate();
    }
}

void VulkanRenderer::createDepthResources() {
    if (!Utils::VulkanFindSupportedFormat(_core.getPhysDevice(), {VK_FORMAT_D24_UNORM_S8_UINT, VK_FORMAT_D32_SFLOAT},
                                          VK_IMAGE_TILING_OPTIMAL, VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT,
                                          _depthBuffer.depthFormat)) {
        Utils::printLog(ERROR_PARAM, "failed to find supported format!");
        return;
    }

    _shadowMapBuffer.depthFormat = _depthBuffer.depthFormat;
    Utils::VulkanCreateImage(_core.getDevice(), _core.getPhysDevice(), _width, _height, _shadowMapBuffer.depthFormat,
                             VK_IMAGE_TILING_OPTIMAL,
                             VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT,
                             VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, _shadowMapBuffer.depthImage, _shadowMapBuffer.depthImageMemory);
    Utils::VulkanCreateImageView(_core.getDevice(), _shadowMapBuffer.depthImage, _shadowMapBuffer.depthFormat,
                                 VK_IMAGE_ASPECT_DEPTH_BIT, _shadowMapBuffer.depthImageView);

    Utils::VulkanCreateImage(_core.getDevice(), _core.getPhysDevice(), _width, _height, _depthBuffer.depthFormat,
                             VK_IMAGE_TILING_OPTIMAL,
                             VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT,
                             VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, _depthBuffer.depthImage, _depthBuffer.depthImageMemory);
    Utils::VulkanCreateImageView(_core.getDevice(), _depthBuffer.depthImage, _depthBuffer.depthFormat, VK_IMAGE_ASPECT_DEPTH_BIT,
                                 _depthBuffer.depthImageView);
}

void VulkanRenderer::init() {
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
