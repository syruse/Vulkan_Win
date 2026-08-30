#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/transform.hpp>

#if defined(USE_FSR) && USE_FSR
#include <ffx_api/vk/ffx_api_vk.hpp>
#endif

#if defined(USE_XESS) && USE_XESS
#include <xess/xess.h>
#endif

#include <array>
#include <chrono>
#include <vector>

#include "CameraPanzer.h"
#include "Particle.h"
#include "PipelineCreatorBase.h"
#include "VulkanState.h"

inline constexpr uint32_t TREES_COUNT = 250;
// Half extent of one perimeter boundary cube instance.
inline constexpr float BOUNDARY_CUBE_HALF_EXTENT = 18.0f;
// Projectile sphere radius used by both visual mesh and hit checks.
inline constexpr float PROJECTILE_RADIUS = 6.0f;
// Initial projectile speed in world units per second.
inline constexpr float PROJECTILE_SPEED = 130.0f;
// How long a fired projectile stays "active" before being force-deactivated.
inline constexpr std::chrono::seconds PROJECTILE_TIMEOUT{8};
// Half-height of a tree's cylinder collider (also used to derive its visual base position).
inline constexpr float TREE_HALF_HEIGHT = 60.0f;

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
    /// Sync projectile render instance with Bullet body transform and auto-hide/deactivate when needed.
    void syncProjectileVisualFromPhysics();
    /// Spawn/launch projectile from tank muzzle if no active projectile exists.
    void tryFireProjectile();
    /// Check if a sphere at position/radius intersects any static boundary cube.
    bool intersectsBoundary(const glm::vec3& position, float radius) const;
#if defined(USE_DLSS) && USE_DLSS
    void setDLSSResourceTags(uint32_t currentImage, const sl::FrameToken& frameToken);
    void setDLSSConstants(const sl::FrameToken& frameToken);
    void evaluateDLSSPass(uint32_t currentImage, const sl::FrameToken& frameToken);
    void applyDLSSOptions();
#endif
#if defined(USE_XESS) && USE_XESS
    void applyXessOptions();
    void evaluateXessPass(uint32_t currentImage);
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
    // Dynamic rigid body of the fired sphere; reused between shots.
    btRigidBody* m_btProjectileBody{nullptr};
    // Static rigid bodies matching visual perimeter cubes.
    std::vector<btRigidBody*> m_btBoundaryBodies{};
    std::vector<btRigidBody*> m_btTreeBodies{};
    std::vector<TreeFallState>  m_btTreeFallStates{};

    // Gameplay props
    // Cached transforms for perimeter cube rendering and simple collision checks.
    std::vector<Instance> m_boundaryCubeInstances{};
    // Indices in m_models for quick access to newly added gameplay models.
    uint32_t m_barrelModelIndex{0u};
    uint32_t m_projectileModelIndex{0u};
    uint32_t m_boundaryModelIndex{0u};
    // Absolute time point after which an in-flight projectile is force-deactivated (set on fire).
    std::chrono::steady_clock::time_point m_projectileTimeoutDeadline{};

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

    CameraPanzer mCamera;
    ViewProj mViewProj{};
    bool m_resetViewProjHistory{true};
    bool m_isDlssEnabled{false};
#if defined(USE_XESS) && USE_XESS
    bool m_isXessEnabled{false};
    bool m_xessResetHistory{true};
    xess_quality_settings_t m_xessQuality{XESS_QUALITY_SETTING_QUALITY};
#endif
    /// Display resolution chosen by the user in the UI combo (independent of upscaler).
    /// Render targets are _offscreenWidth/_offscreenHeight multiplying by UpscalerPreset scale(if enabled);
    /// the upscaler outputs into this resolution; it then gets blitted to the native window resolution.
    uint16_t m_uiDisplayWidth{UI::kResolutions[UI::kDefaultResolutionIdx].width};
    uint16_t m_uiDisplayHeight{UI::kResolutions[UI::kDefaultResolutionIdx].height};
#if defined(USE_DLSS) && USE_DLSS
    sl::DLSSMode m_dlssMode{sl::DLSSMode::eMaxQuality};
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
