#pragma once

#define GLM_FORCE_RADIANS
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <array>

#include "Camera.h"
#include "I3DModel.h"
#include "PipelineCreatorBase.h"
#include "VulkanState.h"

class VulkanRenderer : public VulkanState {
public:
    static constexpr std::string_view MODEL_PATH{"Tank.obj"};

    enum Pipelines { GPASS = 0, TERRAIN, SKYBOX, SHADOWMAP, POST_LIGHTING, POST_FXAA, PARTICLE, MAX };

    VulkanRenderer(std::string_view appName, size_t width, size_t height);

    ~VulkanRenderer();

    void init();

    /// @return: false if exitting is requested
    bool renderScene();

private:
    void cleanupSwapChain();
    void recreateSwapChain(uint16_t width, uint16_t height);

    void createSwapChain();
    void createUniformBuffers();
    void createCommandPool();
    void createCommandBuffer();
    void updateUniformBuffer(uint32_t currentImage);
    void createRenderPass();
    void createPushConstantRange();
    void allocateDynamicBufferTransferSpace();
    void createDescriptorPool();
    void createFramebuffer();
    void createPipeline();
    void recordCommandBuffers(uint32_t currentImage);
    void createSemaphores();
    void createDepthResources();
    void createColorBufferImage();
    void loadModels();
    void recreateDescriptorSets();

    void calculateLightThings();

private:
    uint16_t m_currentFrame = 0u;

    VkRenderPass m_renderPass{nullptr};
    VkPushConstantRange m_pushConstantRange{};

    std::array<VkFramebuffer, MAX_FRAMES_IN_FLIGHT> m_fbs{};

    std::array<std::unique_ptr<PipelineCreatorBase>, Pipelines::MAX> m_pipelineCreators{nullptr};
    std::vector<std::unique_ptr<I3DModel>> m_models{};

    std::array<VkSemaphore, MAX_FRAMES_IN_FLIGHT> m_presentCompleteSem{nullptr};
    std::array<VkSemaphore, MAX_FRAMES_IN_FLIGHT> m_renderCompleteSem{nullptr};
    std::array<VkFence, MAX_FRAMES_IN_FLIGHT> m_drawFences{};

    // intermediate buffer being served for transferring data to gpu memory
    Model* mp_modelTransferSpace{nullptr};

    VkRenderPass m_renderPassFXAA{nullptr};
    std::array<VkFramebuffer, MAX_FRAMES_IN_FLIGHT> m_fbsFXAA{nullptr};

    VkRenderPass m_renderPassShadowMap{nullptr};
    std::array<VkFramebuffer, MAX_FRAMES_IN_FLIGHT> m_fbsShadowMap{nullptr};
    glm::mat4 m_lightProj{1.0f};

    VkRenderPass m_renderPassSemiTrans{nullptr};  // semi-transparent objects will be drawn at the end due to g-pass
    std::array<VkFramebuffer, MAX_FRAMES_IN_FLIGHT> m_fbsSemiTrans{nullptr};

    /// smart ptr for taking over responsibility for lazy init and early removal
    std::unique_ptr<TextureFactory> mTextureFactory{nullptr};

    std::array<std::unique_ptr<I3DModel>, 3u> m_bushes;

    Camera mCamera;
};
