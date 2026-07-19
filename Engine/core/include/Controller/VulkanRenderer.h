#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/transform.hpp>

#if defined(USE_FSR) && USE_FSR
#include <ffx_api/vk/ffx_api_vk.hpp>
#endif

#include <array>
#include <vector>

#include "Camera.h"
#include "Particle.h"
#include "PipelineCreatorBase.h"
#include "VulkanState.h"

#define TREES_COUNT 250

class btDefaultCollisionConfiguration;
class btCollisionDispatcher;
class btBroadphaseInterface;
class btSequentialImpulseConstraintSolver;
class btDiscreteDynamicsWorld;
class btCollisionShape;
class btRigidBody;

struct TreeFallState {
    float baseX = 0.0f;       // world X of the tree base (y=0)
    float baseZ = 0.0f;       // world Z of the tree base
    float axisX = 1.0f;       // fall rotation axis (horizontal)
    float axisZ = 0.0f;
    float angle  = 0.0f;      // current tipping angle [0 .. PI/2]
    bool  falling = false;
};

class VulkanRenderer : public VulkanState {
public:
    enum Pipelines {
        GPASS = 0,
        TERRAIN,
        SKYBOX,
        SHADOWMAP,
        POST_LIGHTING,
        POST_FXAA,
        PARTICLE,
        SEMI_TRANSPARENT,
        GAUSS_X_BLUR,
        GAUSS_Y_BLUR,
        BLOOM,
        DEPTH,
        SSAO,
        FOOTPRINT,
        SSAO_BLUR,
        MAX
    };

    VulkanRenderer(std::string_view appName, uint16_t windowWidth, uint16_t windowHeight);
    ~VulkanRenderer();

    void init();

    /// @return: false if exitting is requested
    bool renderScene();

private:
    void destroyPerFrameResources();
    void cleanupSwapChain();
    void recreateSwapChain(uint16_t windowWidth, uint16_t windowHeight, uint16_t offscreenWidth = 0u,
                           uint16_t offscreenHeight = 0u);

    VkSwapchainCreateInfoKHR createSwapChain();
    void createUniformBuffers();
    void createCommandPool();
    void createCommandBuffer();
    void updateUniformBuffer(uint32_t currentImage, float deltaMS);
    void createRenderPass();
    void allocateDynamicBufferTransferSpace();
    void releaseDynamicBufferTransferSpace();
    void createDescriptorPool();
    void createFramebuffer();
    void createPipeline();
    void recordCommandBuffers(uint32_t currentImage, bool hmiRenderData);
    void createSemaphores();
    void createDescriptorPoolForImGui();
    void createDepthResources();
    void createColorBufferImage();
    void loadModels();
    void recreateDescriptorSets();
    void createFSRContext(VkSwapchainCreateInfoKHR swapchainCreateInfo);
    void calculateAdditionalMat();
#if defined(USE_DLSS) && USE_DLSS
    void setDLSSResourceTags(uint32_t currentImage, const sl::FrameToken& frameToken);
    void setDLSSConstants(const sl::FrameToken& frameToken);
    void evaluateDLSSPass(uint32_t currentImage, const sl::FrameToken& frameToken);
#endif

private:
    uint16_t m_currentFrame = 0u;

    VkPushConstantRange m_pushConstantRange{};

    VkPhysicalDeviceProperties mDeviceProperties;
    std::array<std::unique_ptr<PipelineCreatorBase>, Pipelines::MAX> m_pipelineCreators{nullptr};
    std::vector<std::unique_ptr<I3DModel>> m_models{};
    std::array<std::unique_ptr<Particle>, 5u> m_particles;
    std::vector<I3DModel::InteractionImpactAnimation> m_semiTransparentAnimations{TREES_COUNT};
    std::vector<std::unique_ptr<I3DModel>> m_semiTransparentModels{};

    // Bullet physics state used to drive dynamic transforms (tank + trees).
    btDefaultCollisionConfiguration* m_btCollisionConfig{nullptr};
    btCollisionDispatcher* m_btDispatcher{nullptr};
    btBroadphaseInterface* m_btBroadphase{nullptr};
    btSequentialImpulseConstraintSolver* m_btSolver{nullptr};
    btDiscreteDynamicsWorld* m_btDynamicsWorld{nullptr};
    std::vector<btCollisionShape*> m_btCollisionShapes{};
    btRigidBody* m_btGroundBody{nullptr};
    btRigidBody* m_btTankBody{nullptr};
    std::vector<btRigidBody*> m_btTreeBodies{};
    std::vector<TreeFallState>  m_btTreeFallStates{};
    float m_btTreeHalfHeight{60.0f};

    std::vector<VkSemaphore> m_presentCompleteSem{};
    std::vector<VkSemaphore> m_renderCompleteSem{};
    std::vector<VkFence> m_drawFences{};
    std::vector<bool> m_footprintCleared{};

    // fence per swapchain image tracking
    std::vector<VkFence> m_imagesInFlight;

    // intermediate buffer being served for transferring data to gpu memory
    Model* mp_modelTransferSpace{nullptr};

    // the main renderpass based on G-Pass
    VkRenderPass m_renderPass{nullptr};
    std::vector<VkFramebuffer> m_fbs{};

    VkRenderPass m_renderPassFXAA{nullptr};
    std::vector<VkFramebuffer> m_fbsFXAA{};

    VkRenderPass m_renderPassUIOverlay{nullptr};
    std::vector<VkFramebuffer> m_fbsUIOverlay{};

    VkRenderPass m_renderPassShadowMap{nullptr};
    std::vector<VkFramebuffer> m_fbsShadowMap{};
    glm::mat4 m_lightViewProj{1.0f};

    VkRenderPass m_renderPassSemiTrans{nullptr};  // semi-transparent objects will be drawn at the end due to g-pass
    std::vector<VkFramebuffer> m_fbsSemiTrans{};

    VkRenderPass m_renderPassXBlur{nullptr};  // Gauss x blurring for bloom effect
    std::vector<VkFramebuffer> m_fbsXBlur{};

    VkRenderPass m_renderPassYBlur{nullptr};  // Gauss y blurring for bloom effect
    std::vector<VkFramebuffer> m_fbsYBlur{};

    VkRenderPass m_renderPassBloom{nullptr};
    std::vector<VkFramebuffer> m_fbsBloom{};

    VkRenderPass m_renderPassDepth{nullptr};
    std::vector<VkFramebuffer> m_fbsDepth{};

    VkRenderPass m_renderPassFootprint{nullptr};
    std::vector<VkFramebuffer> m_fbsFootprint{};
    glm::mat4 m_footPrintViewProj{1.0f};

    /** SSAO blurring & applying (for blurring we need the access to neibor pixels hence we have one passthrough _shadingBuffer
       (noisy SSAO content) from subpasses of m_renderPass) thus we have one single shading buffer with noisy SSAO content instead
        keeping two buffers (depth and normals) needed for SSAO creation in separate non subpass aproach
    */
    VkRenderPass m_renderPassSSAOblur{nullptr};
    std::vector<VkFramebuffer> m_fbsSSAOblur{};

    VkDescriptorPool mImguiPool;

    /// smart ptr for taking over responsibility for lazy init and early removal
    std::unique_ptr<TextureFactory> mTextureFactory{nullptr};

    Camera mCamera;
    ViewProj mViewProj{};
    bool m_resetViewProjHistory{true};
    bool m_isDlssEnabled{true};  // TODO : add UI for enabling/disabling DLSS
#if defined(USE_DLSS) && USE_DLSS
    bool m_slTagErrorLogged{false};
    bool m_slConstantsErrorLogged{false};
    uint32_t m_slFrameIndex{0};
    std::vector<bool> m_swapchainImageNeedsGeneralTransition{};
#endif
#if defined(USE_FSR) && USE_FSR
    ffx::Context mFSRSwapChainContext{nullptr};
    ffx::Context mFSRFrameGenContext{nullptr};
    ffx::QueryDescSwapchainReplacementFunctionsVK mFSRReplacementFunctions;
#endif
};
