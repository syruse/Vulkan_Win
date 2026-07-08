#include "VulkanRenderer.h"
#include "MD5Model.h"
#include "ObjModel.h"
#include "Particle.h"
#include "PipelineCreatorFootprint.h"
#include "PipelineCreatorParticle.h"
#include "PipelineCreatorQuad.h"
#include "PipelineCreatorSSAO.h"
#include "PipelineCreatorSemiTransparent.h"
#include "PipelineCreatorShadowMap.h"
#include "PipelineCreatorSkyBox.h"
#include "PipelineCreatorTextured.h"
#include "Skybox.h"
#include "Terrain.h"

#include <assert.h>
#include <algorithm>
#include <chrono>
#include <limits>
#include <random>

#include <imgui/backends/imgui_impl_vulkan.h>
#include <imgui/imgui.h>

#if defined(USE_FSR) && USE_FSR
#include <FidelityFX/host/backends/vk/ffx_vk.h>
#include <ffx_api/ffx_framegeneration.hpp>
#include <ffx_api/ffx_upscale.hpp>
#include <ffx_api/vk/ffx_api_vk.hpp>
#endif

#include <btBulletDynamicsCommon.h>

static constexpr float Z_NEAR = 0.1f;
static constexpr float Z_FAR = 1000.0f;
static constexpr float FOV = 65.0f;

#if defined(USE_DLSS) && USE_DLSS
namespace {
sl::float4x4 toSLRowMajor(const glm::mat4& m) {
    sl::float4x4 out{};
    for (uint32_t r = 0; r < 4; ++r) {
        out[r] = sl::float4(m[0][r], m[1][r], m[2][r], m[3][r]);
    }
    return out;
}
}
#endif

// light source position offset from the camera
const static glm::vec3 _lightPos = glm::vec3(0.0f, 0.6f * Z_FAR, -Z_FAR);
// clear depth buffer only once and then we accumulate trails of the vehicle
static bool _oneOffClearingFootPrint = true;
// lastFootPrintPos allows us to draw original print of wheels without noisy messy effect caused by constant redrawing with
// footprint texture overlapping
static glm::vec3 _lastFootPrintPos = glm::vec3(0.0f, -1000.0f, 0.0f);
// if the traveled distance exceeds 70 percentage of panzer lenght then we draw new footprint
float _footPrintRedrawingK = 0.7f;

VulkanRenderer::VulkanRenderer(std::string_view appName, uint16_t windowWidth, uint16_t windowHeight)
    : VulkanState(appName, windowWidth, windowHeight, 1980, 1024),  // TODO
      mTextureFactory(new TextureFactory(*this)), /// this is not used imedially it's safe
      mCamera({FOV, static_cast<float>(windowWidth) / windowHeight, Z_NEAR, Z_FAR}, {0.0f, 55.0f, -130.0f}) {
    assert(mTextureFactory);
    using namespace std::literals;

    _pushConstant.windowSize = glm::vec4(_windowWidth, _windowHeight, Z_FAR, Z_NEAR);
    _pushConstant.lightPos = glm::vec4(_lightPos, 0.0f);

    calculateAdditionalMat();

    // TODO consider combining into one object with _pushConstant
    m_pushConstantRange.stageFlags = PUSH_CONSTANT_STAGE_FLAGS;
    m_pushConstantRange.offset = 0;
    m_pushConstantRange.size = sizeof(PushConstant);

    assert(m_pipelineCreators.size() == Pipelines::MAX);
    m_pipelineCreators[TERRAIN].reset(new PipelineCreatorTextured(*this, m_renderPass, "vert_terrain.spv", "frag_terrain.spv",
                                                                  "tessCtrl_terrain.spv", "tessEval_terrain.spv", 0U,
                                                                  m_pushConstantRange));
    m_pipelineCreators[GPASS].reset(new PipelineCreatorTextured(*this, m_renderPass, "vert_gPass.spv", "frag_gPass.spv"));
    m_pipelineCreators[SKYBOX].reset(
        new PipelineCreatorSkyBox(*this, m_renderPass, "vert_skybox.spv", "frag_skybox.spv", 0u, m_pushConstantRange));
    m_pipelineCreators[SHADOWMAP].reset(new PipelineCreatorShadowMap(this->_shadowMapBuffer, *this, m_renderPassShadowMap,
                                                                     "vert_shadowMap.spv", "frag_shadowMap.spv"));
    m_pipelineCreators[POST_LIGHTING].reset(new PipelineCreatorQuad(
        *this, m_renderPass, "vert_gLigtingSubpass.spv", "frag_gLigtingSubpass.spv", true, true, 2u, m_pushConstantRange));
    m_pipelineCreators[POST_FXAA].reset(new PipelineCreatorQuad(*this, m_renderPassFXAA, "vert_fxaa.spv", "frag_fxaa.spv",
                                                                &this->_colorBuffer, PipelineCreatorQuad::BLEND::NONE, true,
                                                                m_pushConstantRange, true));
    m_pipelineCreators[PARTICLE].reset(new PipelineCreatorParticle(*this, m_renderPassSemiTrans, "vert_particle.spv",
                                                                   "frag_particle.spv", 0u, m_pushConstantRange));
    m_pipelineCreators[SEMI_TRANSPARENT].reset(new PipelineCreatorSemiTransparent(
        *this, m_renderPassSemiTrans, "vert_semi_transparent.spv", "frag_semi_transparent.spv", 0u, m_pushConstantRange));
    m_pipelineCreators[GAUSS_X_BLUR].reset(
        new PipelineCreatorQuad(*this, m_renderPassXBlur, "vert_gaussXBlur.spv", "frag_gaussXBlur.spv", &this->_bloomBuffer[0]));
    m_pipelineCreators[GAUSS_Y_BLUR].reset(
        new PipelineCreatorQuad(*this, m_renderPassYBlur, "vert_gaussYBlur.spv", "frag_gaussYBlur.spv", &this->_bloomBuffer[1]));
    m_pipelineCreators[BLOOM].reset(new PipelineCreatorQuad(*this, m_renderPassBloom, "vert_bloom.spv", "frag_bloom.spv",
                                                            &this->_bloomBuffer[0],
                                                            PipelineCreatorQuad::BLEND::SRC_ONE_AND_DST_ONE));
    m_pipelineCreators[DEPTH].reset(new PipelineCreatorShadowMap(this->_depthBuffer, *this, m_renderPassDepth,
                                                                 "vert_depthWriter.spv", "frag_depthWriter.spv", true));
    m_pipelineCreators[SSAO].reset(
        new PipelineCreatorSSAO(*this, m_renderPass, "vert_ssao.spv", "frag_ssao.spv", 1u, m_pushConstantRange));
    m_pipelineCreators[FOOTPRINT].reset(new PipelineCreatorFootprint(this->_footprintBuffer, *this, m_renderPassFootprint,
                                                                     "vert_footPrint.spv", "frag_footPrint.spv"));
    m_pipelineCreators[SSAO_BLUR].reset(new PipelineCreatorQuad(*this, m_renderPassSSAOblur, "vert_ssaoBlur.spv",
                                                                "frag_ssaoBlur.spv", &this->_shadingBuffer,
                                                                PipelineCreatorQuad::BLEND::SRC_ALPHA_AND_DST_ONE_MINUS_ALPHA));
    // validation
    for (auto i = 0u; i < Pipelines::MAX; ++i) {
        if (m_pipelineCreators[i] == nullptr) {
            Utils::printLog(ERROR_PARAM, "nullptr pipeline");
            abort();
        }
    }

    m_models.emplace_back(new ObjModel(*this, *mTextureFactory, "Tank.obj"sv,
                                       static_cast<PipelineCreatorTextured*>(m_pipelineCreators[GPASS].get()),
                                       static_cast<PipelineCreatorFootprint*>(m_pipelineCreators[FOOTPRINT].get()), 75.0f));

    m_models.emplace_back(new Terrain(*this, *mTextureFactory, "noise.jpg", "grass1.jpg", "grass2.jpg",
                                      static_cast<PipelineCreatorTextured*>(m_pipelineCreators[TERRAIN].get()), Z_FAR));

    const std::array<std::string_view, 6> skyBoxTextures{"sky_ft.png", "sky_bk.png", "sky_dn.png",
                                                         "sky_up.png", "sky_lt.png", "sky_rt.png"};

    m_models.emplace_back(new Skybox(*this, *mTextureFactory, skyBoxTextures,
                                     static_cast<PipelineCreatorTextured*>(m_pipelineCreators[SKYBOX].get())));

    // we create a lot of trees
    {
        std::vector<Instance> semiTransparentInstances{TREES_COUNT};
        std::random_device rd;
        std::mt19937 gen(rd());  // seed the generator
        float limit = 0.8f * Z_FAR;
        std::uniform_real_distribution<double> distrScale(0.5, 1.0); 
        int32_t gridLen = std::floor(std::sqrt(semiTransparentInstances.size()));
        float step = 2.0f * limit / gridLen;
        // std::uniform_real<> distr(0.0f, 0.1f * step);
        const float startX = -limit;
        const float startZ = -limit;
        for (std::size_t i = 0u; i < semiTransparentInstances.size(); ++i) {
            auto& instance = semiTransparentInstances[i];
            // float xOffset = distr(gen);
            // float zOffset = distr(gen);
            instance.posShift.y = 0.0f;

            instance.scale = distrScale(gen);

            auto row = i / gridLen;
            auto col = i % gridLen;
            instance.posShift.x = startX + row * step;
            instance.posShift.z = startZ + col * step;
        }

        auto lowPolyTrink =
            std::make_unique<ObjModel>(*this, *mTextureFactory, "lowpoly_tree_trunk.obj"sv,
                                       static_cast<PipelineCreatorTextured*>(m_pipelineCreators[SEMI_TRANSPARENT].get()), nullptr,
                                       60.0f, semiTransparentInstances);

        m_semiTransparentModels.emplace_back(
            new ObjModel(*this, *mTextureFactory, "highpoly_tree_trunk.obj"sv,
                         static_cast<PipelineCreatorTextured*>(m_pipelineCreators[SEMI_TRANSPARENT].get()), nullptr, 60.0f,
                         semiTransparentInstances, std::move(lowPolyTrink)));
        m_semiTransparentModels.emplace_back(
            new MD5Model("tree_leaves.md5mesh"sv, "tree_leaves_idle.md5anim"sv, *this, *mTextureFactory,
                         static_cast<PipelineCreatorTextured*>(m_pipelineCreators[SEMI_TRANSPARENT].get()), nullptr, 10.0f, 0.1f,
                         true, semiTransparentInstances));
    }

    m_particles[0] = std::make_unique<Particle>(*this, *mTextureFactory, "bush4.png",
                                                static_cast<PipelineCreatorParticle*>(m_pipelineCreators[PARTICLE].get()), 5000u,
                                                0.85 * Z_FAR, glm::vec3(10.0f, 19.0f, 10.0f));
    m_particles[1] = std::make_unique<Particle>(*this, *mTextureFactory, "bush3.png",
                                                static_cast<PipelineCreatorParticle*>(m_pipelineCreators[PARTICLE].get()), 20000u,
                                                0.85 * Z_FAR, glm::vec3(2.0f, 5.0f, 2.0f));
    m_particles[2] = std::make_unique<Particle>(*this, *mTextureFactory, "bush3.png",
                                                static_cast<PipelineCreatorParticle*>(m_pipelineCreators[PARTICLE].get()), 2000u,
                                                0.85 * Z_FAR, glm::vec3(7.0f, 10.0f, 7.0f));
    m_particles[3] =
        std::make_unique<Particle>(*this, *mTextureFactory, "smoke.png", "smoke_gradient.png",
                                   static_cast<PipelineCreatorParticle*>(m_pipelineCreators[PARTICLE].get()), 250u,
                                   glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, -1.0f), glm::vec3(0.1f), glm::vec3(3.0f));
    m_particles[4] =
        std::make_unique<Particle>(*this, *mTextureFactory, "smoke.png", "smoke_gradient.png",
                                   static_cast<PipelineCreatorParticle*>(m_pipelineCreators[PARTICLE].get()), 250u,
                                   glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, -1.0f), glm::vec3(0.1f), glm::vec3(3.0f));

    // Initialize Bullet
    btDefaultCollisionConfiguration* config = new btDefaultCollisionConfiguration();
    btCollisionDispatcher* dispatcher = new btCollisionDispatcher(config);
    btBroadphaseInterface* broadphase = new btDbvtBroadphase();
    btSequentialImpulseConstraintSolver* solver = new btSequentialImpulseConstraintSolver();
    btDiscreteDynamicsWorld* dynamicsWorld = new btDiscreteDynamicsWorld(dispatcher, broadphase, solver, config);
    dynamicsWorld->setGravity(btVector3(0, -9.81, 0));

    // Creation of static infinite plane at Y = 0.
    {
        // Normal vector of the plane (0, 1, 0) and Distance from origin (0)
        btCollisionShape* groundShape = new btStaticPlaneShape(btVector3(0, 1, 0), 0);
        // For static objects, we can skip the MotionState and use a mass of 0.
        // The local inertia for a static plane is always (0, 0, 0).
        btRigidBody::btRigidBodyConstructionInfo groundRBInfo(0,                  // Mass
                                                              nullptr,            // MotionState (optional for static objects)
                                                              groundShape,        // Collision Shape
                                                              btVector3(0, 0, 0)  // Local Inertia
        );
        btRigidBody* groundBody = new btRigidBody(groundRBInfo);
        dynamicsWorld->addRigidBody(groundBody);
    }

    // Panzer (main vechicle)
    {
        btCollisionShape* boxShape = new btBoxShape(btVector3(1, 1, 1));
        // Create the body with mass (dynamic)
        btScalar mass = 100.0f;
        btVector3 localInertia(0, 0, 0);
        boxShape->calculateLocalInertia(mass, localInertia);
        btTransform startTransform;
        startTransform.setIdentity();
        startTransform.setOrigin(btVector3(0, 0, 0));  // TODO

        btDefaultMotionState* motionState = new btDefaultMotionState(startTransform);
        btRigidBody::btRigidBodyConstructionInfo rbInfo(mass, motionState, boxShape, localInertia);
        btRigidBody* movingBox = new btRigidBody(rbInfo);
        // Set the velocity (x=0, y=0, z=5.0)
        // This will make the box slide or fly along the Z axis at 5 units/sec
        movingBox->setLinearVelocity(btVector3(0, 0, 5.0f));
        // Prevents the box from rotating/tumbling
        movingBox->setAngularFactor(btVector3(0, 0, 0));
        dynamicsWorld->addRigidBody(movingBox);
    }

    // Tree
    {
        btCollisionShape* boxShape =
            new btBoxShape(btVector3(1, 60 / 2.0f, 1));  // Bullet uses half-extents (size 60 / 2.0f means width 60)
        btScalar mass = 1.0f;
        btVector3 localInertia(0, 0, 0);
        boxShape->calculateLocalInertia(mass, localInertia);

        btTransform startTransform;
        startTransform.setIdentity();
        startTransform.setOrigin(btVector3(0, 0, 0));  // TODO
        btDefaultMotionState* motionState = new btDefaultMotionState(startTransform);
        btRigidBody::btRigidBodyConstructionInfo treeRBInfo(mass, motionState, boxShape, localInertia);
        btRigidBody* treeBody = new btRigidBody(treeRBInfo);
        dynamicsWorld->addRigidBody(treeBody);
    }

    //// 5. Run Simulation
    // for (int i = 0; i < 150; i++) {
    //     dynamicsWorld->stepSimulation(1.f / 60.f, 10);
    //     btTransform trans;
    //     fallBody->getMotionState()->getWorldTransform(trans);
    //     std::cout << "Cube Height: " << trans.getOrigin().getY() << std::endl;
    // }
}

VulkanRenderer::~VulkanRenderer() {
    Pipeliner::getInstance().saveCache();

    cleanupSwapChain();
#if defined(USE_FSR) && USE_FSR
    if (mFSRSwapChainContext)
        ffxDestroyContext(&mFSRSwapChainContext, nullptr);
    if (mFSRFrameGenContext)
        ffxDestroyContext(&mFSRFrameGenContext, nullptr);
#endif

    // Explicitly release resources before the core device/instance cleanup.
    for (auto& particle : m_particles) particle.reset();
    m_models.clear();
    m_semiTransparentModels.clear();

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

    // Clear tracking of images-in-flight to avoid stale fences pointing to destroyed resources
    m_imagesInFlight.clear();
    m_swapchainImageCount = 0;

    vkFreeCommandBuffers(_core.getDevice(), _cmdBufPool, static_cast<uint32_t>(_cmdBufs.size()), _cmdBufs.data());

    vkDestroyImageView(_core.getDevice(), _depthBuffer.depthImageView, nullptr);
    vkDestroyImage(_core.getDevice(), _depthBuffer.depthImage, nullptr);
    vkFreeMemory(_core.getDevice(), _depthBuffer.depthImageMemory, nullptr);

    vkDestroyImageView(_core.getDevice(), _depthTempBuffer.depthImageView, nullptr);
    vkDestroyImage(_core.getDevice(), _depthTempBuffer.depthImage, nullptr);
    vkFreeMemory(_core.getDevice(), _depthTempBuffer.depthImageMemory, nullptr);

    vkDestroyImageView(_core.getDevice(), _footprintBuffer.depthImageView, nullptr);
    vkDestroyImage(_core.getDevice(), _footprintBuffer.depthImage, nullptr);
    vkFreeMemory(_core.getDevice(), _footprintBuffer.depthImageMemory, nullptr);

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

        vkDestroyImageView(_core.getDevice(), _ssaoBuffer.colorBufferImageView[i], nullptr);
        vkDestroyImage(_core.getDevice(), _ssaoBuffer.colorBufferImage[i], nullptr);
        vkFreeMemory(_core.getDevice(), _ssaoBuffer.colorBufferImageMemory[i], nullptr);

        vkDestroyImageView(_core.getDevice(), _viewSpaceBuffer.colorBufferImageView[i], nullptr);
        vkDestroyImage(_core.getDevice(), _viewSpaceBuffer.colorBufferImage[i], nullptr);
        vkFreeMemory(_core.getDevice(), _viewSpaceBuffer.colorBufferImageMemory[i], nullptr);

        vkDestroyImageView(_core.getDevice(), _motionVectorsBuffer.colorBufferImageView[i], nullptr);
        vkDestroyImage(_core.getDevice(), _motionVectorsBuffer.colorBufferImage[i], nullptr);
        vkFreeMemory(_core.getDevice(), _motionVectorsBuffer.colorBufferImageMemory[i], nullptr);

        vkDestroyImageView(_core.getDevice(), _shadingBuffer.colorBufferImageView[i], nullptr);
        vkDestroyImage(_core.getDevice(), _shadingBuffer.colorBufferImage[i], nullptr);
        vkFreeMemory(_core.getDevice(), _shadingBuffer.colorBufferImageMemory[i], nullptr);

        vkDestroyImageView(_core.getDevice(), _dlssOutputBuffer.colorBufferImageView[i], nullptr);
        vkDestroyImage(_core.getDevice(), _dlssOutputBuffer.colorBufferImage[i], nullptr);
        vkFreeMemory(_core.getDevice(), _dlssOutputBuffer.colorBufferImageMemory[i], nullptr);

        for (auto& buf : _bloomBuffer) {
            vkDestroyImageView(_core.getDevice(), buf.colorBufferImageView[i], nullptr);
            vkDestroyImage(_core.getDevice(), buf.colorBufferImage[i], nullptr);
            vkFreeMemory(_core.getDevice(), buf.colorBufferImageMemory[i], nullptr);
        }
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
    for (auto& framebuffer : m_fbsXBlur) {
        vkDestroyFramebuffer(_core.getDevice(), framebuffer, nullptr);
    }
    for (auto& framebuffer : m_fbsYBlur) {
        vkDestroyFramebuffer(_core.getDevice(), framebuffer, nullptr);
    }
    for (auto& framebuffer : m_fbsBloom) {
        vkDestroyFramebuffer(_core.getDevice(), framebuffer, nullptr);
    }
    for (auto& framebuffer : m_fbsDepth) {
        vkDestroyFramebuffer(_core.getDevice(), framebuffer, nullptr);
    }
    for (auto& framebuffer : m_fbsFootprint) {
        vkDestroyFramebuffer(_core.getDevice(), framebuffer, nullptr);
    }
    for (auto& framebuffer : m_fbsSSAOblur) {
        vkDestroyFramebuffer(_core.getDevice(), framebuffer, nullptr);
    }

    for (auto& framebuffer : m_fbsUIOverlay) {
        vkDestroyFramebuffer(_core.getDevice(), framebuffer, nullptr);
    }

    for (auto& imageView : _swapChain.views) {
        vkDestroyImageView(_core.getDevice(), imageView, nullptr);
    }

#if defined(USE_FSR) && USE_FSR
    if (mFSRSwapChainContext) {
        mFSRReplacementFunctions.pOutDestroySwapchainFFXAPI(_core.getDevice(), _swapChain.handle, nullptr, mFSRSwapChainContext);
    } else {
        vkDestroySwapchainKHR(_core.getDevice(), _swapChain.handle, nullptr);
    }
#else
    vkDestroySwapchainKHR(_core.getDevice(), _swapChain.handle, nullptr);
#endif

    vkDestroyRenderPass(_core.getDevice(), m_renderPass, nullptr);
    vkDestroyRenderPass(_core.getDevice(), m_renderPassFXAA, nullptr);
    vkDestroyRenderPass(_core.getDevice(), m_renderPassUIOverlay, nullptr);
    vkDestroyRenderPass(_core.getDevice(), m_renderPassShadowMap, nullptr);
    vkDestroyRenderPass(_core.getDevice(), m_renderPassSemiTrans, nullptr);
    vkDestroyRenderPass(_core.getDevice(), m_renderPassXBlur, nullptr);
    vkDestroyRenderPass(_core.getDevice(), m_renderPassYBlur, nullptr);
    vkDestroyRenderPass(_core.getDevice(), m_renderPassBloom, nullptr);
    vkDestroyRenderPass(_core.getDevice(), m_renderPassDepth, nullptr);
    vkDestroyRenderPass(_core.getDevice(), m_renderPassFootprint, nullptr);
    vkDestroyRenderPass(_core.getDevice(), m_renderPassSSAOblur, nullptr);

    for (auto& pipelineCreator : m_pipelineCreators) {
        pipelineCreator->destroyDescriptorPool();
    }
    vkDestroyDescriptorPool(_core.getDevice(), mImguiPool, nullptr);
    ImGui_ImplVulkan_Shutdown();
}

void VulkanRenderer::recreateSwapChain(uint16_t width, uint16_t height) {
    INFO_FORMAT(" new width=%d; new height=%d", width, height);
    if (_windowWidth != width || _windowHeight != height) {
        cleanupSwapChain();

        _windowWidth = width;
        _windowHeight = height;
        m_currentFrame = 0u;
#if defined(USE_DLSS) && USE_DLSS
        m_slFrameIndex = 0u;
#endif
        _oneOffClearingFootPrint = true;
        _lastFootPrintPos = glm::vec3(0.0f, -1000.0f, 0.0f);

        _pushConstant.windowSize = glm::vec4(_windowWidth, _windowHeight, Z_FAR, Z_NEAR);
        calculateAdditionalMat();

        auto swapchainCreateInfo = createSwapChain();
        createCommandBuffer();
        createDepthResources();
        createColorBufferImage();
        createDescriptorPool();
        recreateDescriptorSets();
        createRenderPass();
        createFramebuffer();
        createPipeline();
        createDescriptorPoolForImGui();

#if defined(USE_DLSS) && USE_DLSS
        if (_core.isDlssSupported() && m_isDlssEnabled) {
            static const sl::ViewportHandle viewport(0);
            sl::DLSSOptions options{};
            options.mode = sl::DLSSMode::eMaxQuality;
            options.outputWidth = _windowWidth;
            options.outputHeight = _windowHeight;
            options.colorBuffersHDR = sl::Boolean::eFalse;
            options.useAutoExposure = sl::Boolean::eFalse;
            sl::Result optionsRes = _core.slDLSSSetOptionsSafe(viewport, options);
            if (optionsRes != sl::Result::eOk) {
                Utils::printLog(INFO_PARAM, "slDLSSSetOptions failed after resize, sl::Result=%d", static_cast<int>(optionsRes));
                m_isDlssEnabled = false;
            }
        }
#endif

        createFSRContext(swapchainCreateInfo);
    }
}

// FSR 3 frame generation
void VulkanRenderer::createFSRContext(VkSwapchainCreateInfoKHR swapchainCreateInfo) {
#if defined(USE_FSR) && USE_FSR
    if (mFSRSwapChainContext) {
        return;
    }

    ffx::CreateBackendVKDesc backendDesc{};
    backendDesc.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_VK;
    backendDesc.header.pNext = nullptr;
    backendDesc.vkDevice = _core.getDevice();
    backendDesc.vkPhysicalDevice = _core.getPhysDevice();
    backendDesc.vkDeviceProcAddr = vkGetDeviceProcAddr;

    ffx::CreateContextDescFrameGenerationSwapChainVK createSwapChainDesc{};
    createSwapChainDesc.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_FGSWAPCHAIN_VK;
    createSwapChainDesc.header.pNext = nullptr;
    createSwapChainDesc.device = _core.getDevice();
    createSwapChainDesc.physicalDevice = _core.getPhysDevice();
    createSwapChainDesc.swapchain = &_swapChain.handle;
    createSwapChainDesc.createInfo = swapchainCreateInfo;
    createSwapChainDesc.allocator = nullptr;

    createSwapChainDesc.gameQueue.queue = _queue;
    createSwapChainDesc.gameQueue.familyIndex = _core.getQueueFamily();
    createSwapChainDesc.gameQueue.submitFunc =
        nullptr;  // this queue is only used in vkQueuePresentKHR, hence doesn't need a callback
    auto& asyncComputeQueue = _core.getAllQueues().at(VulkanCore::Queue_family::FSR_ASYNC_COMPUTE_QUEUE_FAMILY);
    createSwapChainDesc.asyncComputeQueue.queue = asyncComputeQueue.queue;
    createSwapChainDesc.asyncComputeQueue.familyIndex = asyncComputeQueue.familyIndex;
    createSwapChainDesc.asyncComputeQueue.submitFunc = nullptr;
    auto& presentQueue = _core.getAllQueues().at(VulkanCore::Queue_family::FSR_PRESENT_QUEUE_FAMILY);
    createSwapChainDesc.presentQueue.queue = presentQueue.queue;
    createSwapChainDesc.presentQueue.familyIndex = presentQueue.familyIndex;
    createSwapChainDesc.presentQueue.submitFunc = nullptr;
    auto& imageAcquireQueue = _core.getAllQueues().at(VulkanCore::Queue_family::FSR_IMAGE_ACQUIRE_QUEUE_FAMILY);
    createSwapChainDesc.imageAcquireQueue.queue = imageAcquireQueue.queue;
    createSwapChainDesc.imageAcquireQueue.familyIndex = imageAcquireQueue.familyIndex;
    createSwapChainDesc.imageAcquireQueue.submitFunc = nullptr;

    auto convertQueueInfo = [](VkQueueInfoFFXAPI queueInfo) {
        VkQueueInfoFFX info;
        info.queue = queueInfo.queue;
        info.familyIndex = queueInfo.familyIndex;
        info.submitFunc = queueInfo.submitFunc;
        return info;
    };

    VkFrameInterpolationInfoFFX frameInterpolationInfo = {};
    frameInterpolationInfo.device = createSwapChainDesc.device;
    frameInterpolationInfo.physicalDevice = createSwapChainDesc.physicalDevice;
    frameInterpolationInfo.pAllocator = createSwapChainDesc.allocator;
    frameInterpolationInfo.gameQueue = convertQueueInfo(createSwapChainDesc.gameQueue);
    frameInterpolationInfo.asyncComputeQueue = convertQueueInfo(createSwapChainDesc.asyncComputeQueue);
    frameInterpolationInfo.presentQueue = convertQueueInfo(createSwapChainDesc.presentQueue);
    frameInterpolationInfo.imageAcquireQueue = convertQueueInfo(createSwapChainDesc.imageAcquireQueue);

    ffx::ReturnCode retCode = ffx::CreateContext(mFSRSwapChainContext, nullptr, createSwapChainDesc, backendDesc);
    if (retCode != ffx::ReturnCode::Ok) {
        Utils::printLog(ERROR_PARAM, "Failed to create FSR SwapChain context", static_cast<uint32_t>(retCode));
    }

    if (!mFSRSwapChainContext) {
        Utils::printLog(ERROR_PARAM, "FSR SwapChain context is null after creation");
        return;
    }

    // Get replacement function pointers
    ffx::Query(mFSRSwapChainContext, mFSRReplacementFunctions);

    ffx::QueryDescGetVersions versionQuery{};
    versionQuery.createDescType = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE;
    uint64_t versionCount = 0;
    versionQuery.outputCount = &versionCount;
    ffxQuery(nullptr, &versionQuery.header);

    std::vector<const char*> versionNames;
    std::vector<uint64_t> m_FsrVersionIds;
    m_FsrVersionIds.resize(versionCount);
    versionNames.resize(versionCount);
    versionQuery.versionIds = m_FsrVersionIds.data();
    versionQuery.versionNames = versionNames.data();
    ffxQuery(nullptr, &versionQuery.header);

    ffx::ConfigureDescFrameGenerationSwapChainKeyValueVK m_swapchainKeyValueConfig{};
    m_swapchainKeyValueConfig.key = FFX_API_CONFIGURE_FG_SWAPCHAIN_KEY_WAITCALLBACK;
    m_swapchainKeyValueConfig.ptr = nullptr;
    ffx::Configure(mFSRSwapChainContext, m_swapchainKeyValueConfig);

    ffx::CreateContextDescFrameGeneration createFg{};
    createFg.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATION;
    createFg.header.pNext = reinterpret_cast<ffxApiHeader*>(&backendDesc);
    //createFg.displaySize = {_windowWidth, _windowHeight};
    //createFg.maxRenderSize = {_windowWidth, _windowHeight};
    createFg.displaySize.width  = swapchainCreateInfo.imageExtent.width;
    createFg.displaySize.height = swapchainCreateInfo.imageExtent.height;
    createFg.maxRenderSize.width = swapchainCreateInfo.imageExtent.width;
    createFg.maxRenderSize.height = swapchainCreateInfo.imageExtent.height;
    //createFg.flags = FFX_FRAMEGENERATION_ENABLE_DEBUG_CHECKING;
    createFg.flags = FFX_FRAMEGENERATION_ENABLE_ASYNC_WORKLOAD_SUPPORT;
    createFg.backBufferFormat = ffxApiGetSurfaceFormatVK(swapchainCreateInfo.imageFormat);

    retCode = ffx::CreateContext(mFSRFrameGenContext, nullptr, createFg);
    if (retCode != ffx::ReturnCode::Ok) {
        Utils::printLog(ERROR_PARAM, "Failed to create FSR FG context. Code: ", static_cast<uint32_t>(retCode));
        return;
    }

    if (!mFSRFrameGenContext) {
        Utils::printLog(ERROR_PARAM, "FSR FrameGen context is null after creation");
        return;
    }

    Utils::printLog(INFO_PARAM, "FSR contexts created successfully");
#endif
}

void VulkanRenderer::recreateDescriptorSets() {
    for (auto& pipelineCreator : m_pipelineCreators) {
        pipelineCreator->recreateDescriptors();
    }
}

void VulkanRenderer::calculateAdditionalMat() {
    // up vector is flipped for footPrint :vec3(0.0f, 0.0f, 1.0f) since we have 90 degree angle of view point
    const glm::vec3 footPrintUp = glm::vec3(0.0f, 0.0f, 1.0f);
    m_footPrintViewProj = glm::ortho(Z_FAR, -Z_FAR, -Z_FAR, Z_FAR, -Z_FAR, Z_FAR) *
                          glm::lookAt(glm::vec3(0.0f, Z_FAR, 0.0f), glm::vec3(0.0f, 0.0f, 0.0f), footPrintUp);
    m_footPrintViewProj[1][1] *= -1;
}

void VulkanRenderer::updateUniformBuffer(uint32_t currentImage, float deltaMS) {
    assert(_ubo.buffersMemory.size() > currentImage);
    const float kDelay = deltaMS;

    static const glm::mat4 identityMatrix = glm::mat4(1.0f);

    glm::vec4 velocity = mCamera.targetModelMat() * 4.0f * glm::vec4(0.0f, 0.0f, -1.0f, 0.0f);
    glm::vec4 exhaustPipePos1 =
        mCamera.targetModelMat() *
        glm::vec4(-4.0f, 19.0f, -30.0f, 1.0f);  // Note: here we use hardcoded position of pipe in our model!!!
    m_particles[3]->update(currentImage, deltaMS, exhaustPipePos1, velocity);
    glm::vec4 exhaustPipePos2 =
        mCamera.targetModelMat() *
        glm::vec4(4.0f, 19.0f, -30.0f, 1.0f);  // Note: here we use hardcoded position of pipe in our model!!!
    m_particles[4]->update(currentImage, deltaMS, exhaustPipePos2, velocity);

    const auto objectsAmount = m_models.size();

    const auto& cameraViewProj = mCamera.viewProjMat();
    const auto& model = mCamera.targetModelMat();

    static bool firstViewProjUpdate = true;
    const glm::mat4 currentViewProj = cameraViewProj.proj * cameraViewProj.view;
    mViewProj.prevViewProj = firstViewProjUpdate ? currentViewProj : mViewProj.viewProj;
    mViewProj.viewProj = currentViewProj;
    firstViewProjUpdate = false;
    mViewProj.viewProjInverse = glm::inverse(mViewProj.viewProj);
    mViewProj.lightViewProj = m_lightViewProj;
    mViewProj.proj = cameraViewProj.proj;
    mViewProj.view = cameraViewProj.view;
    mViewProj.footPrintViewProj = m_footPrintViewProj;

    // Copy VP data
    void* data;
    vkMapMemory(_core.getDevice(), _ubo.buffersMemory[currentImage], 0, sizeof(mViewProj), 0, &data);
    memcpy(data, &mViewProj, sizeof(mViewProj));
    vkUnmapMemory(_core.getDevice(), _ubo.buffersMemory[currentImage]);

    // Copy Model data except skybox
    for (size_t i = 1u; i < objectsAmount - 1; i++) {
        Model* pModel = (Model*)((uint64_t)mp_modelTransferSpace + (i * _modelUniformAlignment));
        pModel->prevModel = pModel->model;
        pModel->model = identityMatrix;
        pModel->MVP = mViewProj.viewProj;
    }
    // set target model matrix from Camera for our main 3d model
    Model* pModel = (Model*)((uint64_t)mp_modelTransferSpace);
    pModel->prevModel = pModel->model;
    pModel->model = model;
    pModel->MVP = mViewProj.viewProj * pModel->model;

    // rotate skybox slowly
    pModel = (Model*)((uint64_t)mp_modelTransferSpace + (objectsAmount - 1) * _modelUniformAlignment);
    static float skyboxRotationDegree = 0.0f;
    skyboxRotationDegree += 0.0001f * kDelay;
    glm::mat4 rotMat = glm::rotate(glm::radians(static_cast<float>(skyboxRotationDegree)), glm::vec3(0.0f, 1.0f, 0.0f));
    pModel->prevModel = pModel->model;
    pModel->model = rotMat;
    pModel->MVP = mViewProj.proj * glm::mat4(glm::mat3(cameraViewProj.view)) * pModel->model;

    // trees consist of 2 models: obj(static trunk) and md5(leaves/crown) with identical instances data
    auto& treeTrunkInstances = m_semiTransparentModels[0]->instances();
    auto& treeCrownInstances = m_semiTransparentModels[1]->instances();
    for (size_t i = 0; i < TREES_COUNT; i++) {
        auto& treeTrunkInstance = treeTrunkInstances[i];

        if (m_semiTransparentAnimations[i].isAnimationStarted()) {
            m_semiTransparentAnimations[i].updateModelMat(deltaMS);
            const glm::mat4& modelMat = m_semiTransparentAnimations[i].getModelMat();
            auto& treeCrownInstance = treeCrownInstances[i];

            //----------- Store previous model matrix columns for motion vector calculations-------//
            treeTrunkInstance.prev_model_col0 = treeTrunkInstance.model_col0;
            treeTrunkInstance.prev_model_col1 = treeTrunkInstance.model_col1;
            treeTrunkInstance.prev_model_col2 = treeTrunkInstance.model_col2;
            treeTrunkInstance.prev_model_col3 = treeTrunkInstance.model_col3;

            treeCrownInstance.prev_model_col0 = treeCrownInstance.model_col0;
            treeCrownInstance.prev_model_col1 = treeCrownInstance.model_col1;
            treeCrownInstance.prev_model_col2 = treeCrownInstance.model_col2;
            treeCrownInstance.prev_model_col3 = treeCrownInstance.model_col3;

            //----------- Store current model matrix columns for general purpose usage-------//
            treeTrunkInstance.model_col0 = glm::packHalf4x16(modelMat[0]);
            treeTrunkInstance.model_col1 = glm::packHalf4x16(modelMat[1]);
            treeTrunkInstance.model_col2 = glm::packHalf4x16(modelMat[2]);
            treeTrunkInstance.model_col3 = glm::packHalf4x16(modelMat[3]);

            treeCrownInstance.model_col0 = treeTrunkInstance.model_col0;
            treeCrownInstance.model_col1 = treeTrunkInstance.model_col1;
            treeCrownInstance.model_col2 = treeTrunkInstance.model_col2;
            treeCrownInstance.model_col3 = treeTrunkInstance.model_col3;
            continue;
        }

        auto tarpos = mCamera.targetPos();
        auto dist = glm::distance(treeTrunkInstance.posShift, mCamera.targetPos());
        auto boundingRadiuses =
            0.5f * m_models[0]->radius();  // we can skip it for trees '+m_semiTransparentModels[0]->radius() * instance.scale;'
        if (boundingRadiuses >= dist) {
            m_semiTransparentAnimations[i].startAnimation(2000.0f);  // 2 seconds
        }
    }

    for (size_t i = objectsAmount; i < (objectsAmount + m_semiTransparentModels.size()); i++) {
        Model* pModel = (Model*)((uint64_t)mp_modelTransferSpace + (i * _modelUniformAlignment));
        pModel->prevModel = pModel->model;
        pModel->model = m_semiTransparentAnimations[0].getModelMat();
        pModel->MVP = mViewProj.viewProj;
    }

    // smoothly move the light source towards the desired position along Z axis,
    // but only if it's significantly different from the current position.

    // Stable light positioning (Texel Snapping)
    glm::vec3 tankPos = mCamera.targetPos();
    glm::vec3 desiredLightPos = tankPos + _lightPos;

    // We "jump" the light source only when the tank moves significantly (10% of Z_FAR).
    // This keeps static shadows (like trees) from flickering or shifting during micro-movements.
    if (glm::distance(glm::vec3(_pushConstant.lightPos), desiredLightPos) > 0.1f * Z_FAR) {
        // Shadow map resolution is _shadowMapWidthAndHeight (8000x8000).
        // Texel snapping aligns the light frustum to the shadow map grid to prevent sub-texel flickering.
        // The total orthographic width is 2.2 * Z_FAR (covering from -1.1 to 1.1).
        float texelSize = (Z_FAR * 2.2f) / _shadowMapWidthAndHeight;
        _pushConstant.lightPos = glm::vec4(glm::floor(desiredLightPos / texelSize) * texelSize, _pushConstant.lightPos.w);
    }

    // We keep a constant direction vector: from -1000 to +1000 in Z (length 3500).
    // Normalizing the direction ensures that shadow angles remain perfectly static when the light moves.
    glm::vec3 lightDir = glm::normalize(glm::vec3(0.0f, -_lightPos.y, -_lightPos.z)) * (Z_FAR * 3.5f);
    glm::vec3 target = glm::vec3(_pushConstant.lightPos) + lightDir;

    // A shadow side of 1.1 * Z_FAR provides extra padding to prevent shadow cutoff at screen corners.
    const float shadowSide = Z_FAR * 1.1f;
    m_lightViewProj =
        // from light source to center(sqrt{900^2 + 1000^2} = ~1345+)
        // Far=4000 is sufficient, to cover the distance to the tank and the terrain beyond it.
        // Near plane at -500.0f captures high objects (like tall trees) that might be "behind" the light source.
        glm::ortho(-shadowSide, shadowSide, -shadowSide, shadowSide, -500.0f, Z_FAR * 4.0f) *
        glm::lookAt(glm::vec3(_pushConstant.lightPos), target, glm::vec3(0.0f, 1.0f, 0.0f));
    m_lightViewProj[1][1] *= -1;

    // Map the list of model data
    vkMapMemory(_core.getDevice(), _dynamicUbo.buffersMemory[currentImage], 0,
                _modelUniformAlignment * (objectsAmount + m_semiTransparentModels.size()), 0, &data);
    memcpy(data, mp_modelTransferSpace, _modelUniformAlignment * (objectsAmount + m_semiTransparentModels.size()));
    vkUnmapMemory(_core.getDevice(), _dynamicUbo.buffersMemory[currentImage]);
}

void VulkanRenderer::allocateDynamicBufferTransferSpace() {
    size_t minUniformBufferOffset = static_cast<size_t>(mDeviceProperties.limits.minUniformBufferOffsetAlignment);

    // Calculate alignment of model matrix data
    _modelUniformAlignment = (sizeof(Model) + minUniformBufferOffset - 1) & ~(minUniformBufferOffset - 1);

    // Create space in memory to hold dynamic buffer that is aligned to our required alignment and holds m_models.size()
    mp_modelTransferSpace = (Model*)_aligned_malloc(_modelUniformAlignment * (m_models.size() + m_semiTransparentModels.size()),
                                                    _modelUniformAlignment);
}

void VulkanRenderer::createDescriptorPool() {
    for (auto& pipelineCreator : m_pipelineCreators) {
        pipelineCreator->createDescriptorPool();
    }
}

VkSwapchainCreateInfoKHR VulkanRenderer::createSwapChain() {
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
    SwapChainCreateInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    if (SurfaceCaps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT) {
        SwapChainCreateInfo.imageUsage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    }
    if (SurfaceCaps.supportedUsageFlags & VK_IMAGE_USAGE_STORAGE_BIT) {
        SwapChainCreateInfo.imageUsage |= VK_IMAGE_USAGE_STORAGE_BIT;
    }
    SwapChainCreateInfo.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    SwapChainCreateInfo.imageArrayLayers = 1;
    SwapChainCreateInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    SwapChainCreateInfo.presentMode = presentMode;
    SwapChainCreateInfo.clipped = VK_TRUE;
    SwapChainCreateInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;

    VkResult res;
#if defined(USE_FSR) && USE_FSR
    if (mFSRSwapChainContext) {
        res = mFSRReplacementFunctions.pOutCreateSwapchainFFXAPI(_core.getDevice(), &SwapChainCreateInfo, nullptr,
                                                                 &_swapChain.handle, mFSRSwapChainContext);
    } else {
        res = vkCreateSwapchainKHR(_core.getDevice(), &SwapChainCreateInfo, nullptr, &_swapChain.handle);
    }
#else
    res = vkCreateSwapchainKHR(_core.getDevice(), &SwapChainCreateInfo, nullptr, &_swapChain.handle);
#endif
    CHECK_VULKAN_ERROR("vkCreateSwapchainKHR error %d\n", res);

    Utils::printLog(INFO_PARAM, "Swap chain created");

    uint32_t NumSwapChainImages = 0;
#if defined(USE_FSR) && USE_FSR
    if (mFSRSwapChainContext) {
        res = mFSRReplacementFunctions.pOutGetSwapchainImagesKHR(_core.getDevice(), _swapChain.handle, &NumSwapChainImages,
                                                                 nullptr);
    } else {
        res = vkGetSwapchainImagesKHR(_core.getDevice(), _swapChain.handle, &NumSwapChainImages, nullptr);
    }
#else
    res = vkGetSwapchainImagesKHR(_core.getDevice(), _swapChain.handle, &NumSwapChainImages, nullptr);
#endif
    CHECK_VULKAN_ERROR("vkGetSwapchainImagesKHR error %d\n", res);
    assert(MAX_FRAMES_IN_FLIGHT <= NumSwapChainImages);
    Utils::printLog(INFO_PARAM, "Available number of presentable images ", NumSwapChainImages);
#if defined(USE_FSR) && USE_FSR
    if (mFSRSwapChainContext) {
        res = mFSRReplacementFunctions.pOutGetSwapchainImagesKHR(_core.getDevice(), _swapChain.handle, &NumSwapChainImages,
                                                                 &(_swapChain.images[0]));
    } else {
        res = vkGetSwapchainImagesKHR(_core.getDevice(), _swapChain.handle, &NumSwapChainImages, &(_swapChain.images[0]));
    }
#else
    res = vkGetSwapchainImagesKHR(_core.getDevice(), _swapChain.handle, &NumSwapChainImages, &(_swapChain.images[0]));
#endif
    CHECK_VULKAN_ERROR("vkGetSwapchainImagesKHR error %d\n", res);

    // initialize image-in-flight tracking
    m_swapchainImageCount = NumSwapChainImages;
    m_imagesInFlight.assign(static_cast<size_t>(m_swapchainImageCount), VK_NULL_HANDLE);

#if defined(USE_DLSS) && USE_DLSS
    m_swapchainImageNeedsGeneralTransition.fill(false);
#endif

    return SwapChainCreateInfo;
}

void VulkanRenderer::createUniformBuffers() {
    // ViewProjection buffer size
    VkDeviceSize bufferSize = sizeof(ViewProj);

    // Model buffer size
    VkDeviceSize modelBufferSize = _modelUniformAlignment * (m_models.size() + m_semiTransparentModels.size());

    /**
     * We should have multiple buffers, because multiple frames may be in flight at the same time and
     * we don't want to update the buffer in preparation of the next frame while a previous one is still reading from it!
     */
    for (size_t i = 0; i < _swapChain.images.size(); i++) {
        Utils::VulkanCreateBuffer(_core.getDevice(), _core.getPhysDevice(), bufferSize,
                                  VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, _ubo.buffers[i],
                                  _ubo.buffersMemory[i]);
        Utils::VulkanCreateBuffer(_core.getDevice(), _core.getPhysDevice(), modelBufferSize,
                                  VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
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
    cmdBufAllocInfo.commandBufferCount = static_cast<uint32_t>(_swapChain.images.size());

    VkResult res = vkAllocateCommandBuffers(_core.getDevice(), &cmdBufAllocInfo, &_cmdBufs[0]);
    CHECK_VULKAN_ERROR("vkAllocateCommandBuffers error %d\n", res);

    Utils::printLog(INFO_PARAM, "Created command buffers");
}

void VulkanRenderer::recordCommandBuffers(uint32_t currentImage, bool hmiRenderData) {
    static VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, nullptr,
                                              VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT, nullptr};

    VkResult res = vkBeginCommandBuffer(_cmdBufs[currentImage], &beginInfo);
    CHECK_VULKAN_ERROR("vkBeginCommandBuffer error %d\n", res);

    const static VkClearValue zeroClearValues{{0.0f, 0.0f, 0.0f, 0.0f}};

    //---------------------------------------------------------------------------------------------//
    /// depth writing pass (depth + view space pos + motion vectors)
    static std::array<VkClearValue, 3> depthWriterClearValues{zeroClearValues, zeroClearValues, zeroClearValues};
    depthWriterClearValues[0].depthStencil.depth = 1.0f;

    VkRenderPassBeginInfo renderPassdepthWriterInfo = {};
    renderPassdepthWriterInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassdepthWriterInfo.renderPass = m_renderPassDepth;
    renderPassdepthWriterInfo.renderArea.offset.x = 0;
    renderPassdepthWriterInfo.renderArea.offset.y = 0;
    renderPassdepthWriterInfo.renderArea.extent.width = _depthBuffer.width;
    renderPassdepthWriterInfo.renderArea.extent.height = _depthBuffer.height;
    renderPassdepthWriterInfo.clearValueCount = static_cast<uint32_t>(depthWriterClearValues.size());
    renderPassdepthWriterInfo.pClearValues = depthWriterClearValues.data();
    renderPassdepthWriterInfo.framebuffer = m_fbsDepth[currentImage];

    vkCmdBeginRenderPass(_cmdBufs[currentImage], &renderPassdepthWriterInfo, VK_SUBPASS_CONTENTS_INLINE);

    // depth writing for each object
    for (uint32_t meshIndex = 0u; meshIndex < m_models.size(); ++meshIndex) {
        const uint32_t dynamicOffset = static_cast<uint32_t>(_modelUniformAlignment) * meshIndex;
        m_models[meshIndex]->drawWithCustomPipeline(m_pipelineCreators[DEPTH].get(), _cmdBufs[currentImage], currentImage,
                                                    dynamicOffset);
    }

    vkCmdEndRenderPass(_cmdBufs[currentImage]);

    // No explicit barrier needed here: m_renderPassDepth already ends _depthBuffer in
    // VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL (attachment finalLayout).

    // No explicit barier needed for _viewSpaceBuffer: COLOR_ATTACHMENT_OPTIMAL -> SHADER_READ_ONLY_OPTIMAL
    // This is because m_renderPassDepth ends _viewSpaceBuffer in VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL (attachment
    // finalLayout).

    //---------------------------------------------------------------------------------------------//
    /// shadow map pass
    VkClearValue shadowMapClearValues{};
    shadowMapClearValues.depthStencil.depth = 1.0f;

    VkRenderPassBeginInfo renderPassShadowMapInfo = {};
    renderPassShadowMapInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassShadowMapInfo.renderPass = m_renderPassShadowMap;
    renderPassShadowMapInfo.renderArea.offset.x = 0;
    renderPassShadowMapInfo.renderArea.offset.y = 0;
    renderPassShadowMapInfo.renderArea.extent.width = _shadowMapBuffer.width;
    renderPassShadowMapInfo.renderArea.extent.height = _shadowMapBuffer.height;
    renderPassShadowMapInfo.clearValueCount = 1;
    renderPassShadowMapInfo.pClearValues = &shadowMapClearValues;
    renderPassShadowMapInfo.framebuffer = m_fbsShadowMap[currentImage];

    vkCmdBeginRenderPass(_cmdBufs[currentImage], &renderPassShadowMapInfo, VK_SUBPASS_CONTENTS_INLINE);

    // draw shadow of 3d mesh only
    for (uint32_t meshIndex = 0u; meshIndex < m_models.size() - 2u; ++meshIndex) {
        const uint32_t dynamicOffset = static_cast<uint32_t>(_modelUniformAlignment) * meshIndex;
        m_models[meshIndex]->drawWithCustomPipeline(m_pipelineCreators[SHADOWMAP].get(), _cmdBufs[currentImage], currentImage,
                                                    dynamicOffset);
    }

    for (uint32_t meshIndex = 0u; meshIndex < m_semiTransparentModels.size(); ++meshIndex) {
        const uint32_t dynamicOffset = static_cast<uint32_t>(_modelUniformAlignment) * (meshIndex + m_models.size());
        m_semiTransparentModels[meshIndex]->drawWithCustomPipeline(m_pipelineCreators[SHADOWMAP].get(), _cmdBufs[currentImage],
                                                                   currentImage, dynamicOffset);
    }

    vkCmdEndRenderPass(_cmdBufs[currentImage]);

    //---------------------------------------------------------------------------------------------//
    /// footprint pass
    VkRenderPassBeginInfo renderPassFootprintInfo = {};
    renderPassFootprintInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassFootprintInfo.renderPass = m_renderPassFootprint;
    renderPassFootprintInfo.renderArea.offset.x = 0;
    renderPassFootprintInfo.renderArea.offset.y = 0;
    renderPassFootprintInfo.renderArea.extent.width = _footprintBuffer.width;
    renderPassFootprintInfo.renderArea.extent.height = _footprintBuffer.height;
    renderPassFootprintInfo.framebuffer = m_fbsFootprint[currentImage];

    vkCmdBeginRenderPass(_cmdBufs[currentImage], &renderPassFootprintInfo, VK_SUBPASS_CONTENTS_INLINE);

    if (_oneOffClearingFootPrint) {
        VkClearValue footPrintClearValues{};
        footPrintClearValues.depthStencil.depth = 1.0f;
        VkClearAttachment clearAttachment{};
        clearAttachment.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        clearAttachment.clearValue = footPrintClearValues;
        clearAttachment.colorAttachment = 0u;
        VkClearRect clearRect = {{{0u, 0u}, {_footprintBuffer.width, _footprintBuffer.height}}, 0u, 1u};
        vkCmdClearAttachments(_cmdBufs[currentImage], 1, &clearAttachment, 1u, &clearRect);
        _oneOffClearingFootPrint = false;
    } else if (glm::distance(_lastFootPrintPos, mCamera.targetPos()) >= _footPrintRedrawingK * m_models[0]->radius()) {
        // draw object tracks (the panzer will leave the footprint)
        uint32_t meshIndex = 0u;
        const uint32_t dynamicOffset = static_cast<uint32_t>(_modelUniformAlignment) * meshIndex;
        m_models[meshIndex]->drawFootprints(_cmdBufs[currentImage], currentImage, dynamicOffset);
        _lastFootPrintPos = mCamera.targetPos();
    }

    vkCmdEndRenderPass(_cmdBufs[currentImage]);

    //---------------------------------------------------------------------------------------------//
    /// G pass
    VkClearValue clearValue{};
    clearValue.color = {0.0f, 0.0f, 0.0f, 1.0f};
    // no need to clear MotionVector buffer, since we will write to it in the first subpass
    std::vector<VkClearValue> clearValues(10, clearValue);
    clearValues[7] = VkClearValue{};
    clearValues[7].depthStencil.depth = 1.0f;

    VkRenderPassBeginInfo renderPassInfo = {};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = m_renderPass;
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent.width = _offscreenWidth;
    renderPassInfo.renderArea.extent.height = _offscreenHeight;
    renderPassInfo.clearValueCount = clearValues.size();
    renderPassInfo.pClearValues = clearValues.data();
    renderPassInfo.framebuffer = m_fbs[currentImage];

    /** Note:
     * We remove the manual barrier here, since RenderPass will automatically make the transition
     * from UNDEFINED (initialLayout) to COLOR_ATTACHMENT_OPTIMAL.
     * If the barrier is needed, the initialLayout in RenderPass must be
     * VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL.
     * Utils::VulkanImageMemoryBarrier(_cmdBufs[currentImage], _colorBuffer.colorBufferImage[currentImage],
     * _colorBuffer.colorFormat, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT,
     * 1U, 1U, 0, 0, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT);
     */

    vkCmdBeginRenderPass(_cmdBufs[currentImage], &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

    ///  SkyBox and 3D Models
    for (uint32_t meshIndex = 0u; meshIndex < m_models.size(); ++meshIndex) {
        const uint32_t dynamicOffset = static_cast<uint32_t>(_modelUniformAlignment) * meshIndex;
        m_models[meshIndex]->draw(_cmdBufs[currentImage], currentImage, dynamicOffset);
    }

    ///-----------------------------------------------------------------------------------///
    /// Start second subpass (SSAO)
    vkCmdNextSubpass(_cmdBufs[currentImage], VK_SUBPASS_CONTENTS_INLINE);

    /// quad subpass
    {
        const auto& pipelineCreator = m_pipelineCreators[SSAO];
        vkCmdPushConstants(_cmdBufs[currentImage], pipelineCreator->getPipeline()->pipelineLayout, PUSH_CONSTANT_STAGE_FLAGS, 0,
                           sizeof(PushConstant), &_pushConstant);
        vkCmdBindPipeline(_cmdBufs[currentImage], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineCreator->getPipeline()->pipeline);
        vkCmdBindDescriptorSets(_cmdBufs[currentImage], VK_PIPELINE_BIND_POINT_GRAPHICS,
                                pipelineCreator->getPipeline()->pipelineLayout, 0, 1,
                                pipelineCreator->getDescriptorSet(currentImage), 0, nullptr);
    }

    vkCmdDraw(_cmdBufs[currentImage], 6, 1, 0, 0);

    ///-----------------------------------------------------------------------------------///
    /// Start third subpass
    vkCmdNextSubpass(_cmdBufs[currentImage], VK_SUBPASS_CONTENTS_INLINE);

    /// quad subpass
    {
        const auto& pipelineCreator = m_pipelineCreators[POST_LIGHTING];
        vkCmdPushConstants(_cmdBufs[currentImage], pipelineCreator->getPipeline()->pipelineLayout, PUSH_CONSTANT_STAGE_FLAGS, 0,
                           sizeof(PushConstant), &_pushConstant);

        vkCmdBindPipeline(_cmdBufs[currentImage], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineCreator->getPipeline()->pipeline);
        vkCmdBindDescriptorSets(_cmdBufs[currentImage], VK_PIPELINE_BIND_POINT_GRAPHICS,
                                pipelineCreator->getPipeline()->pipelineLayout, 0, 1,
                                pipelineCreator->getDescriptorSet(currentImage), 0, nullptr);
    }

    vkCmdDraw(_cmdBufs[currentImage], 6, 1, 0, 0);

    vkCmdEndRenderPass(_cmdBufs[currentImage]);

    //---------------------------------------------------------------------------------------------//
    // SSAO BLUR
    static std::array<VkClearValue, 2> ssaoBlurClearValues{zeroClearValues, zeroClearValues};

    VkRenderPassBeginInfo renderPassSSAOblurInfo = {};
    renderPassSSAOblurInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassSSAOblurInfo.renderPass = m_renderPassSSAOblur;
    renderPassSSAOblurInfo.renderArea.offset = {0, 0};
    renderPassSSAOblurInfo.renderArea.extent.width = _offscreenWidth;
    renderPassSSAOblurInfo.renderArea.extent.height = _offscreenHeight;
    renderPassSSAOblurInfo.clearValueCount = ssaoBlurClearValues.size();
    renderPassSSAOblurInfo.pClearValues = ssaoBlurClearValues.data();
    renderPassSSAOblurInfo.framebuffer = m_fbsSSAOblur[currentImage];

    // SSAO Blur: shading buffer already ends the main render pass in SHADER_READ_ONLY_OPTIMAL (after G-pass),
    // so no explicit layout transition is needed here.
    vkCmdBeginRenderPass(_cmdBufs[currentImage], &renderPassSSAOblurInfo, VK_SUBPASS_CONTENTS_INLINE);
    {
        const auto& pipelineCreator = m_pipelineCreators[SSAO_BLUR];
        vkCmdBindPipeline(_cmdBufs[currentImage], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineCreator->getPipeline()->pipeline);
        vkCmdBindDescriptorSets(_cmdBufs[currentImage], VK_PIPELINE_BIND_POINT_GRAPHICS,
                                pipelineCreator->getPipeline()->pipelineLayout, 0, 1,
                                pipelineCreator->getDescriptorSet(currentImage), 0, nullptr);
    }

    vkCmdDraw(_cmdBufs[currentImage], 6, 1, 0, 0);

    vkCmdEndRenderPass(_cmdBufs[currentImage]);

    //---------------------------------------------------------------------------------------------//
    // 3 times gauss blurring
    for (int32_t t = 0; t < 3; ++t) {
        /// GAUSS X Bloom render pass
        static std::array<VkClearValue, 2> gaussXBloomClearValues{zeroClearValues, zeroClearValues};

        VkRenderPassBeginInfo renderPassGaussXBloomInfo = {};
        renderPassGaussXBloomInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        renderPassGaussXBloomInfo.renderPass = m_renderPassXBlur;
        renderPassGaussXBloomInfo.renderArea.offset = {0, 0};
        renderPassGaussXBloomInfo.renderArea.extent.width = _offscreenWidth;
        renderPassGaussXBloomInfo.renderArea.extent.height = _offscreenHeight;
        renderPassGaussXBloomInfo.clearValueCount = gaussXBloomClearValues.size();
        renderPassGaussXBloomInfo.pClearValues = gaussXBloomClearValues.data();
        renderPassGaussXBloomInfo.framebuffer = m_fbsXBlur[currentImage];

        vkCmdBeginRenderPass(_cmdBufs[currentImage], &renderPassGaussXBloomInfo, VK_SUBPASS_CONTENTS_INLINE);
        {
            const auto& pipelineCreator = m_pipelineCreators[GAUSS_X_BLUR];
            vkCmdBindPipeline(_cmdBufs[currentImage], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineCreator->getPipeline()->pipeline);
            vkCmdBindDescriptorSets(_cmdBufs[currentImage], VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    pipelineCreator->getPipeline()->pipelineLayout, 0, 1,
                                    pipelineCreator->getDescriptorSet(currentImage), 0, nullptr);
        }

        vkCmdDraw(_cmdBufs[currentImage], 6, 1, 0, 0);

        vkCmdEndRenderPass(_cmdBufs[currentImage]);

        /// GAUSS Y Bloom render pass
        static std::array<VkClearValue, 2> gaussYBloomClearValues{zeroClearValues, zeroClearValues};

        VkRenderPassBeginInfo renderPassGaussYBloomInfo = {};
        renderPassGaussYBloomInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        renderPassGaussYBloomInfo.renderPass = m_renderPassYBlur;
        renderPassGaussYBloomInfo.renderArea.offset = {0, 0};
        renderPassGaussYBloomInfo.renderArea.extent.width = _offscreenWidth;
        renderPassGaussYBloomInfo.renderArea.extent.height = _offscreenHeight;
        renderPassGaussYBloomInfo.clearValueCount = gaussYBloomClearValues.size();
        renderPassGaussYBloomInfo.pClearValues = gaussYBloomClearValues.data();
        renderPassGaussYBloomInfo.framebuffer = m_fbsYBlur[currentImage];

        vkCmdBeginRenderPass(_cmdBufs[currentImage], &renderPassGaussYBloomInfo, VK_SUBPASS_CONTENTS_INLINE);
        {
            const auto& pipelineCreator = m_pipelineCreators[GAUSS_Y_BLUR];
            vkCmdBindPipeline(_cmdBufs[currentImage], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineCreator->getPipeline()->pipeline);
            vkCmdBindDescriptorSets(_cmdBufs[currentImage], VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    pipelineCreator->getPipeline()->pipelineLayout, 0, 1,
                                    pipelineCreator->getDescriptorSet(currentImage), 0, nullptr);
        }

        vkCmdDraw(_cmdBufs[currentImage], 6, 1, 0, 0);

        vkCmdEndRenderPass(_cmdBufs[currentImage]);
    }

    //---------------------------------------------------------------------------------------------//
    /// BLOOM

    static std::array<VkClearValue, 2> bloomClearValues{zeroClearValues, zeroClearValues};

    VkRenderPassBeginInfo renderPassBloomInfo = {};
    renderPassBloomInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassBloomInfo.renderPass = m_renderPassBloom;
    renderPassBloomInfo.renderArea.offset = {0, 0};
    renderPassBloomInfo.renderArea.extent.width = _offscreenWidth;
    renderPassBloomInfo.renderArea.extent.height = _offscreenHeight;
    renderPassBloomInfo.clearValueCount = bloomClearValues.size();
    renderPassBloomInfo.pClearValues = bloomClearValues.data();
    renderPassBloomInfo.framebuffer = m_fbsBloom[currentImage];

    vkCmdBeginRenderPass(_cmdBufs[currentImage], &renderPassBloomInfo, VK_SUBPASS_CONTENTS_INLINE);
    {
        const auto& pipelineCreator = m_pipelineCreators[BLOOM];
        vkCmdBindPipeline(_cmdBufs[currentImage], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineCreator->getPipeline()->pipeline);
        vkCmdBindDescriptorSets(_cmdBufs[currentImage], VK_PIPELINE_BIND_POINT_GRAPHICS,
                                pipelineCreator->getPipeline()->pipelineLayout, 0, 1,
                                pipelineCreator->getDescriptorSet(currentImage), 0, nullptr);
    }

    vkCmdDraw(_cmdBufs[currentImage], 6, 1, 0, 0);

    vkCmdEndRenderPass(_cmdBufs[currentImage]);

    //---------------------------------------------------------------------------------------------//
    /// SEMI-TRANSPARENT OBJECTS render pass

    static std::array<VkClearValue, 2> semiTransClearValues{zeroClearValues, zeroClearValues};

    VkRenderPassBeginInfo renderPassSemiTransInfo = {};
    renderPassSemiTransInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassSemiTransInfo.renderPass = m_renderPassSemiTrans;
    renderPassSemiTransInfo.renderArea.offset = {0, 0};
    renderPassSemiTransInfo.renderArea.extent.width = _offscreenWidth;
    renderPassSemiTransInfo.renderArea.extent.height = _offscreenHeight;
    renderPassSemiTransInfo.clearValueCount = semiTransClearValues.size();
    renderPassSemiTransInfo.pClearValues = semiTransClearValues.data();
    renderPassSemiTransInfo.framebuffer = m_fbsSemiTrans[currentImage];

    // The depth image was sampled as read-only earlier (SSAO/lighting). Re-open it as a depth attachment
    // so the semi-transparent pass can run depth test and update depth.
    Utils::VulkanImageMemoryBarrier(_cmdBufs[currentImage], _depthBuffer.depthImage, _depthBuffer.depthFormat,
                                    VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
                                    VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL, VK_IMAGE_ASPECT_DEPTH_BIT, 1U, 1U,
                                    VK_ACCESS_SHADER_READ_BIT,
                                    VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                                    VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT);

    vkCmdBeginRenderPass(_cmdBufs[currentImage], &renderPassSemiTransInfo, VK_SUBPASS_CONTENTS_INLINE);
    {
        const auto& pipelineCreator = m_pipelineCreators[PARTICLE];
        vkCmdPushConstants(_cmdBufs[currentImage], pipelineCreator->getPipeline()->pipelineLayout, PUSH_CONSTANT_STAGE_FLAGS, 0,
                           sizeof(PushConstant), &_pushConstant);
        for (auto& particle : m_particles) {
            particle->draw(_cmdBufs[currentImage], currentImage);
        }
    }

    for (uint32_t meshIndex = 0u; meshIndex < m_semiTransparentModels.size(); ++meshIndex) {
        // TODO
        const auto& pipelineCreator = m_pipelineCreators[SEMI_TRANSPARENT];
        vkCmdPushConstants(_cmdBufs[currentImage], pipelineCreator->getPipeline()->pipelineLayout, PUSH_CONSTANT_STAGE_FLAGS, 0,
                           sizeof(PushConstant), &_pushConstant);
        const uint32_t dynamicOffset = static_cast<uint32_t>(_modelUniformAlignment) * (meshIndex + m_models.size());
        m_semiTransparentModels[meshIndex]->draw(_cmdBufs[currentImage], currentImage, dynamicOffset);
    }
    vkCmdEndRenderPass(_cmdBufs[currentImage]);

    // Semi-transparent pass uses depth as writable attachment; switch back to read-only
    // before DLSS tags/evaluation and for a stable end-of-frame layout.
    Utils::VulkanImageMemoryBarrier(
        _cmdBufs[currentImage], _depthBuffer.depthImage, _depthBuffer.depthFormat,
        VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
        VK_IMAGE_ASPECT_DEPTH_BIT, 1U, 1U,
        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
        VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);

    Utils::VulkanImageMemoryBarrier(
        _cmdBufs[currentImage], _colorBuffer.colorBufferImage[currentImage], _colorBuffer.colorFormat,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT,
        1U, 1U, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);

    // After writing motion vectors in the semi-transparent pass, transition back to shader-read layout
    // so subsequent post-process stages (TAA/FSR/DLAA) can safely sample this texture.
    Utils::VulkanImageMemoryBarrier(
        _cmdBufs[currentImage], _motionVectorsBuffer.colorBufferImage[currentImage], _motionVectorsBuffer.colorFormat,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT,
        1U, 1U, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);

    bool isDlssFrameTokenValid = true;
#if defined(USE_DLSS) && USE_DLSS
    sl::FrameToken* dlssFrameToken = nullptr;
    if (_core.isDlssSupported() && m_isDlssEnabled) {
        sl::Result frameTokenRes = _core.slGetNewFrameTokenSafe(dlssFrameToken, &m_slFrameIndex);
        if (frameTokenRes != sl::Result::eOk || !dlssFrameToken) {
            isDlssFrameTokenValid = false; 
            if (!m_slConstantsErrorLogged) {
                Utils::printLog(INFO_PARAM, "slGetNewFrameToken failed, sl::Result=%d", static_cast<int>(frameTokenRes));
                m_slConstantsErrorLogged = true;
            }
        } else {
            setDLSSConstants(*dlssFrameToken);
            // Tag the final per-frame DLSS inputs once all producer passes have completed.
            setDLSSResourceTags(currentImage, *dlssFrameToken);
            evaluateDLSSPass(currentImage, *dlssFrameToken);

            if (m_slConstantsErrorLogged) {
                m_isDlssEnabled = false;
                Utils::printLog(INFO_PARAM, "DLSS disabled due to slGetNewFrameToken failure");
            }

            if (hmiRenderData) {
                VkRenderPassBeginInfo renderPassUIInfo = {};
                renderPassUIInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
                renderPassUIInfo.renderPass = m_renderPassUIOverlay;
                renderPassUIInfo.renderArea.offset.x = 0;
                renderPassUIInfo.renderArea.offset.y = 0;
                renderPassUIInfo.renderArea.extent.width = _windowWidth;
                renderPassUIInfo.renderArea.extent.height = _windowHeight;
                renderPassUIInfo.clearValueCount = 0;
                renderPassUIInfo.pClearValues = nullptr;
                renderPassUIInfo.framebuffer = m_fbsUIOverlay[currentImage];

                vkCmdBeginRenderPass(_cmdBufs[currentImage], &renderPassUIInfo, VK_SUBPASS_CONTENTS_INLINE);
                _core.getWinController()->imGuiNewFrame(_cmdBufs[currentImage]);
                vkCmdEndRenderPass(_cmdBufs[currentImage]);
            }
        }
        ++m_slFrameIndex;
    }
#endif

    if (!m_isDlssEnabled || !_core.isDlssSupported() || !isDlssFrameTokenValid) {
        //---------------------------------------------------------------------------------------------//
        /// FXAA render pass (FINAL PASS) render with native resolution!

        static std::array<VkClearValue, 2> fxaaClearValues;
        fxaaClearValues[0].color = {0.0f, 0.0f, 0.0f, 1.0f};
        fxaaClearValues[1].color = {0.0f, 0.0f, 0.0f, 1.0f};

        VkRenderPassBeginInfo renderPassFXAAInfo = {};
        renderPassFXAAInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        renderPassFXAAInfo.renderPass = m_renderPassFXAA;
        renderPassFXAAInfo.renderArea.offset.x = 0;
        renderPassFXAAInfo.renderArea.offset.y = 0;
        renderPassFXAAInfo.renderArea.extent.width = _windowWidth;
        renderPassFXAAInfo.renderArea.extent.height = _windowHeight;
        renderPassFXAAInfo.clearValueCount = fxaaClearValues.size();
        renderPassFXAAInfo.pClearValues = fxaaClearValues.data();
        renderPassFXAAInfo.framebuffer = m_fbsFXAA[currentImage];

        // FXAA render pass begins with the color buffer already in SHADER_READ_ONLY_OPTIMAL after semi-transparent pass.
        // The render pass begin will handle the layout transition if necessary.
        vkCmdBeginRenderPass(_cmdBufs[currentImage], &renderPassFXAAInfo, VK_SUBPASS_CONTENTS_INLINE);
        {
            const auto& pipelineCreator = m_pipelineCreators[POST_FXAA];
            PushConstant fxaaPushConstant = _pushConstant;
            // FXAA samples from the offscreen color buffer, so texel size must match offscreen dimensions.
            fxaaPushConstant.windowSize.x = static_cast<float>(_offscreenWidth);
            fxaaPushConstant.windowSize.y = static_cast<float>(_offscreenHeight);
            vkCmdPushConstants(_cmdBufs[currentImage], pipelineCreator->getPipeline()->pipelineLayout, PUSH_CONSTANT_STAGE_FLAGS, 0,
                               sizeof(PushConstant), &fxaaPushConstant);
            vkCmdBindPipeline(_cmdBufs[currentImage], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineCreator->getPipeline()->pipeline);
            vkCmdBindDescriptorSets(_cmdBufs[currentImage], VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    pipelineCreator->getPipeline()->pipelineLayout, 0, 1,
                                    pipelineCreator->getDescriptorSet(currentImage), 0, nullptr);
        }

        vkCmdDraw(_cmdBufs[currentImage], 6, 1, 0, 0);
        vkCmdEndRenderPass(_cmdBufs[currentImage]);

        if (hmiRenderData) {
            VkRenderPassBeginInfo renderPassUIInfo = {};
            renderPassUIInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
            renderPassUIInfo.renderPass = m_renderPassUIOverlay;
            renderPassUIInfo.renderArea.offset.x = 0;
            renderPassUIInfo.renderArea.offset.y = 0;
            renderPassUIInfo.renderArea.extent.width = _windowWidth;
            renderPassUIInfo.renderArea.extent.height = _windowHeight;
            renderPassUIInfo.clearValueCount = 0;
            renderPassUIInfo.pClearValues = nullptr;
            renderPassUIInfo.framebuffer = m_fbsUIOverlay[currentImage];

            vkCmdBeginRenderPass(_cmdBufs[currentImage], &renderPassUIInfo, VK_SUBPASS_CONTENTS_INLINE);
            _core.getWinController()->imGuiNewFrame(_cmdBufs[currentImage]);
            vkCmdEndRenderPass(_cmdBufs[currentImage]);
        }
    }

    res = vkEndCommandBuffer(_cmdBufs[currentImage]);
    CHECK_VULKAN_ERROR("vkEndCommandBuffer error %d\n", res);
}

void VulkanRenderer::createColorBufferImage() {
    // Get supported format for color attachment

    if (!Utils::VulkanFindSupportedFormat(_core.getPhysDevice(), {VK_FORMAT_R8G8B8A8_UNORM}, VK_IMAGE_TILING_OPTIMAL,
                                          VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT, _colorBuffer.colorFormat)) {
        Utils::printLog(ERROR_PARAM, "failed to find supported format!");
    }
    _gPassBuffer.color.colorFormat = _colorBuffer.colorFormat;

    if (!Utils::VulkanFindSupportedFormat(_core.getPhysDevice(), {VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_R16G16B16A16_SFLOAT},
                                          VK_IMAGE_TILING_OPTIMAL, VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT,
                                          _gPassBuffer.normal.colorFormat)) {
        Utils::printLog(ERROR_PARAM, "failed to find supported format!");
    }
    _viewSpaceBuffer.colorFormat = _gPassBuffer.normal.colorFormat;

    if (!Utils::VulkanFindSupportedFormat(_core.getPhysDevice(), {VK_FORMAT_R16G16_SFLOAT},
                                          VK_IMAGE_TILING_OPTIMAL, VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT, 
                                          _motionVectorsBuffer.colorFormat)) {
        Utils::printLog(ERROR_PARAM, "failed to find supported format!");
    }

    auto HDRFormat = VK_FORMAT_UNDEFINED;
    if (!Utils::VulkanFindSupportedFormat(_core.getPhysDevice(), {VK_FORMAT_R16G16B16A16_SFLOAT, VK_FORMAT_R32G32B32A32_SFLOAT},
                                          VK_IMAGE_TILING_OPTIMAL, VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT, HDRFormat)) {
        Utils::printLog(ERROR_PARAM, "failed to find supported format!");
    }

    if (!Utils::VulkanFindSupportedFormat(_core.getPhysDevice(), {VK_FORMAT_R16_SFLOAT, VK_FORMAT_R32_SFLOAT},
                                          VK_IMAGE_TILING_OPTIMAL, VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT,
                                          _ssaoBuffer.colorFormat)) {
        Utils::printLog(ERROR_PARAM, "failed to find supported format!");
    }
    _shadingBuffer.colorFormat = _ssaoBuffer.colorFormat;
    _dlssOutputBuffer.colorFormat = _colorBuffer.colorFormat;

    for (size_t i = 0; i < _swapChain.images.size(); ++i) {
        // By keeping G Pass buffers on-tile only (VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT), we can save a lot of bandwidth and
        // memory. we don't need to write the g-buffer data out to memory let's leave everything in tile memory,
        // do the lighting pass within the tile (you read them as input attachments), and then forget them
        // In this sample, only _colorBuffer needs to be written out to memory and be used out of subpasses

        // Create Color Buffer Image
        Utils::VulkanCreateImage(
            _core.getDevice(), _core.getPhysDevice(), _offscreenWidth, _offscreenHeight,
                                 _colorBuffer.colorFormat, VK_IMAGE_TILING_OPTIMAL,
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            _colorBuffer.colorBufferImage[i], _colorBuffer.colorBufferImageMemory[i]);

        // Create Color Buffer Image View
        Utils::VulkanCreateImageView(_core.getDevice(), _colorBuffer.colorBufferImage[i], _colorBuffer.colorFormat,
                                     VK_IMAGE_ASPECT_COLOR_BIT, _colorBuffer.colorBufferImageView[i]);

        // Optional, Pre-transition to SHADER_READ_ONLY_OPTIMAL so G-pass initialLayout matches on the first frame.
        // to sync with colorAttachment.initialLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        Utils::VulkanTransitionImageLayout(_core.getDevice(), _queue, _cmdBufPool,
                           _colorBuffer.colorBufferImage[i], _colorBuffer.colorFormat,
                           VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                           VK_IMAGE_ASPECT_COLOR_BIT, 1U, 1U);

        // the same applied to G pass buffer

        Utils::VulkanCreateImage(
            _core.getDevice(), _core.getPhysDevice(), _offscreenWidth, _offscreenHeight,
            _gPassBuffer.normal.colorFormat, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                                                                           VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT |
                                                                           VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, _gPassBuffer.normal.colorBufferImage[i],
            _gPassBuffer.normal.colorBufferImageMemory[i]);
        Utils::VulkanCreateImageView(_core.getDevice(), _gPassBuffer.normal.colorBufferImage[i], _gPassBuffer.normal.colorFormat,
                                     VK_IMAGE_ASPECT_COLOR_BIT, _gPassBuffer.normal.colorBufferImageView[i]);

        Utils::VulkanCreateImage(
            _core.getDevice(), _core.getPhysDevice(), _offscreenWidth, _offscreenHeight,
            _gPassBuffer.color.colorFormat, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                                                                          VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT |
                                                                          VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, _gPassBuffer.color.colorBufferImage[i],
            _gPassBuffer.color.colorBufferImageMemory[i]);
        Utils::VulkanCreateImageView(_core.getDevice(), _gPassBuffer.color.colorBufferImage[i], _gPassBuffer.color.colorFormat,
                                     VK_IMAGE_ASPECT_COLOR_BIT, _gPassBuffer.color.colorBufferImageView[i]);

        // HDR render targets for Bloom effect
        for (size_t bufIndex = 0u; bufIndex < _bloomBuffer.size(); ++bufIndex) {
            auto& buf = _bloomBuffer[bufIndex];
            buf.colorFormat = HDRFormat;
            Utils::VulkanCreateImage(_core.getDevice(), _core.getPhysDevice(), _offscreenWidth, _offscreenHeight, buf.colorFormat,
                                     VK_IMAGE_TILING_OPTIMAL,
                                     VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT |
                                         VK_IMAGE_USAGE_SAMPLED_BIT,
                                     VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, buf.colorBufferImage[i], buf.colorBufferImageMemory[i]);

            Utils::VulkanCreateImageView(_core.getDevice(), buf.colorBufferImage[i], buf.colorFormat, VK_IMAGE_ASPECT_COLOR_BIT,
                                         buf.colorBufferImageView[i]);

            if (bufIndex == 1u) {
                Utils::VulkanTransitionImageLayout(_core.getDevice(), _queue, _cmdBufPool, buf.colorBufferImage[i],
                                                   buf.colorFormat, VK_IMAGE_LAYOUT_UNDEFINED,
                                                   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT, 1U, 1U);
            }
        }

        // SSAO render target
        Utils::VulkanCreateImage(_core.getDevice(), _core.getPhysDevice(), _offscreenWidth, _offscreenHeight,
                                 _ssaoBuffer.colorFormat, VK_IMAGE_TILING_OPTIMAL,
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, _ssaoBuffer.colorBufferImage[i], _ssaoBuffer.colorBufferImageMemory[i]);

        Utils::VulkanCreateImageView(_core.getDevice(), _ssaoBuffer.colorBufferImage[i], _ssaoBuffer.colorFormat,
                                     VK_IMAGE_ASPECT_COLOR_BIT, _ssaoBuffer.colorBufferImageView[i]);

        // view space position render target
        Utils::VulkanCreateImage(_core.getDevice(), _core.getPhysDevice(), _offscreenWidth, _offscreenHeight,
                                 _viewSpaceBuffer.colorFormat, VK_IMAGE_TILING_OPTIMAL,
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            _viewSpaceBuffer.colorBufferImage[i], _viewSpaceBuffer.colorBufferImageMemory[i]);

        Utils::VulkanCreateImageView(_core.getDevice(), _viewSpaceBuffer.colorBufferImage[i], _viewSpaceBuffer.colorFormat,
                                     VK_IMAGE_ASPECT_COLOR_BIT, _viewSpaceBuffer.colorBufferImageView[i]);

        // motion vectors render target
        Utils::VulkanCreateImage(
            _core.getDevice(), _core.getPhysDevice(), _offscreenWidth, _offscreenHeight, _motionVectorsBuffer.colorFormat,
            VK_IMAGE_TILING_OPTIMAL,
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, _motionVectorsBuffer.colorBufferImage[i],
            _motionVectorsBuffer.colorBufferImageMemory[i]);

        Utils::VulkanCreateImageView(_core.getDevice(), _motionVectorsBuffer.colorBufferImage[i], _motionVectorsBuffer.colorFormat,
                                     VK_IMAGE_ASPECT_COLOR_BIT, _motionVectorsBuffer.colorBufferImageView[i]);

        // SHADING render target
        Utils::VulkanCreateImage(_core.getDevice(), _core.getPhysDevice(), _offscreenWidth, _offscreenHeight,
                                 _shadingBuffer.colorFormat, VK_IMAGE_TILING_OPTIMAL,
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            _shadingBuffer.colorBufferImage[i], _shadingBuffer.colorBufferImageMemory[i]);

        Utils::VulkanCreateImageView(_core.getDevice(), _shadingBuffer.colorBufferImage[i], _shadingBuffer.colorFormat,
                                     VK_IMAGE_ASPECT_COLOR_BIT, _shadingBuffer.colorBufferImageView[i]);

        Utils::VulkanCreateImage(_core.getDevice(), _core.getPhysDevice(), _windowWidth, _windowHeight,
                                 _dlssOutputBuffer.colorFormat, VK_IMAGE_TILING_OPTIMAL,
                                 VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                                     VK_IMAGE_USAGE_SAMPLED_BIT,
                                 VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, _dlssOutputBuffer.colorBufferImage[i],
                                 _dlssOutputBuffer.colorBufferImageMemory[i]);

        Utils::VulkanCreateImageView(_core.getDevice(), _dlssOutputBuffer.colorBufferImage[i], _dlssOutputBuffer.colorFormat,
                                     VK_IMAGE_ASPECT_COLOR_BIT, _dlssOutputBuffer.colorBufferImageView[i]);

        Utils::VulkanTransitionImageLayout(_core.getDevice(), _queue, _cmdBufPool, _dlssOutputBuffer.colorBufferImage[i],
                                           _dlssOutputBuffer.colorFormat, VK_IMAGE_LAYOUT_UNDEFINED,
                                           VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_ASPECT_COLOR_BIT, 1U, 1U);
    }
}

void VulkanRenderer::loadModels() {
    /// lazy init when core is ready
    mTextureFactory->init();

    for (auto& model : m_models) {
        model->init();
    }

    for (auto& model : m_semiTransparentModels) {
        model->init();
    }

    for (auto& particle : m_particles) {
        particle->init();
    }

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
    static auto endTime = std::chrono::high_resolution_clock::now();
    static float deltaTime = 0.0f;

    mCamera.update(deltaTime, true);

    auto windowQueueMSG = winController->processWindowQueueMSGs();  /// falls into NRVO
    ret_status = !windowQueueMSG.isQuited;

    if (windowQueueMSG.isResized && windowQueueMSG.width > 0 && windowQueueMSG.height > 0) {
        mCamera.resetPerspective({FOV, (float)windowQueueMSG.width / windowQueueMSG.height, Z_NEAR, Z_FAR});
        recreateSwapChain(windowQueueMSG.width, windowQueueMSG.height);
        return ret_status;
    }

    // USER INPUT handling
    if (windowQueueMSG.buttonFlag & IControl::WindowQueueMSG::UP) {
        _footPrintRedrawingK = 0.7f;
        mCamera.move(Camera::EDirection::Forward);
    }
    if (windowQueueMSG.buttonFlag & IControl::WindowQueueMSG::LEFT) {
        _footPrintRedrawingK = 0.03f;
        mCamera.move(Camera::EDirection::Left);
    }
    if (windowQueueMSG.buttonFlag & IControl::WindowQueueMSG::RIGHT) {
        _footPrintRedrawingK = 0.03f;
        mCamera.move(Camera::EDirection::Right);
    }
    if (windowQueueMSG.buttonFlag & IControl::WindowQueueMSG::DONW) {
        _footPrintRedrawingK = 0.7f;
        mCamera.move(Camera::EDirection::Back);
    }

    _pushConstant.cameraPos = glm::vec4(mCamera.cameraPosition(), mDeviceProperties.limits.maxTessellationGenerationLevel);
    _pushConstant.lightPos.w = _pushConstant.windDirElapsedTimeMS.w;  // previous frame's elapsed time
    _pushConstant.windDirElapsedTimeMS.w += deltaTime;

    // -- GET NEXT IMAGE --
    // Wait for fence of this frame to ensure CPU won't overwrite resources currently used by GPU.
    vkWaitForFences(_core.getDevice(), 1, &m_drawFences[m_currentFrame], VK_TRUE, UINT64_MAX);
    // NOTE: DO NOT reset fence here. We'll reset it immediately before vkQueueSubmit.

    uint32_t ImageIndex = 0;
    VkResult res;
#if defined(USE_FSR) && USE_FSR
    if (mFSRSwapChainContext) {
        res = mFSRReplacementFunctions.pOutAcquireNextImageKHR(_core.getDevice(), _swapChain.handle, UINT64_MAX,
                                                               m_presentCompleteSem[m_currentFrame], VK_NULL_HANDLE, &ImageIndex);
    } else {
        res = vkAcquireNextImageKHR(_core.getDevice(), _swapChain.handle, UINT64_MAX, m_presentCompleteSem[m_currentFrame],
                                    VK_NULL_HANDLE, &ImageIndex);
    }
#else
    res = vkAcquireNextImageKHR(_core.getDevice(), _swapChain.handle, UINT64_MAX, m_presentCompleteSem[m_currentFrame],
                                VK_NULL_HANDLE, &ImageIndex);
#endif

    // If acquire returned VK_ERROR_OUT_OF_DATE_KHR, we will process the resize.
    if (res == VK_ERROR_OUT_OF_DATE_KHR) {
        recreateSwapChain(_windowWidth, _windowHeight);
        return ret_status;
    }
    CHECK_VULKAN_ERROR("vkAcquireNextImageKHR error %d\n", res);

    // --- ensure the acquired swapchain image is not still in use by a previous frame
    VkFence imageFence = m_imagesInFlight[ImageIndex];
    // WE WAIT ONLY IF this fence belongs to another frame and it is not yet completed
    if (imageFence != VK_NULL_HANDLE && imageFence != m_drawFences[m_currentFrame]) {
        // Let's wait until the previous use of this image ends.
        vkWaitForFences(_core.getDevice(), 1, &imageFence, VK_TRUE, UINT64_MAX);
    }
    // mark the image as occupied by the current frame-fence (we'll assign it later, before submitting—but let's save the link now)
    m_imagesInFlight[ImageIndex] = m_drawFences[m_currentFrame];

    VkPipelineStageFlags waitFlags = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submitInfo = {};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &_cmdBufs[ImageIndex];
    
    // Wait for the semaphore of the current virtual frame (m_currentFrame)
    submitInfo.pWaitSemaphores = &m_presentCompleteSem[m_currentFrame]; 
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitDstStageMask = &waitFlags;
    
    // Signal the semaphore unique to this Swapchain image (ImageIndex)
    submitInfo.pSignalSemaphores = &m_renderCompleteSem[ImageIndex]; 
    submitInfo.signalSemaphoreCount = 1;

    updateUniformBuffer(ImageIndex, deltaTime);
    static bool isGPUCalculationFavorable = true;
    if (windowQueueMSG.hmiStates) {
        isGPUCalculationFavorable = windowQueueMSG.hmiStates->gpuAnimationEnabled.second;

        // TODO Check for resolution change from the UI states
        // if (windowQueueMSG.hmiStates->resolutionChanged) {
        // }
    }

    for (auto& model : m_models) {
        model->update(deltaTime, 0, isGPUCalculationFavorable, ImageIndex, mViewProj.viewProj, Z_FAR, mCamera.cameraPosition());
    }

    for (auto& model : m_semiTransparentModels) {
        model->update(deltaTime, 0, isGPUCalculationFavorable, ImageIndex, mViewProj.viewProj, Z_FAR, mCamera.cameraPosition());
    }

    recordCommandBuffers(ImageIndex, windowQueueMSG.hmiRenderData);

    // RESET fence of the current frame just before submit
    vkResetFences(_core.getDevice(), 1, &m_drawFences[m_currentFrame]);

    res = vkQueueSubmit(_queue, 1, &submitInfo, m_drawFences[m_currentFrame]);
    CHECK_VULKAN_ERROR("vkQueueSubmit error %d\n", res);

    // --- PREPARE PRESENT INFO ---
    VkPresentInfoKHR presentInfo = {};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &_swapChain.handle;
    presentInfo.pImageIndices = &ImageIndex;
    
    // We pass the rendering semaphore associated with this particular Swapchainimage.
    presentInfo.pWaitSemaphores = &m_renderCompleteSem[ImageIndex]; 
    presentInfo.waitSemaphoreCount = 1;

#if defined(USE_FSR) && USE_FSR
    if (mFSRSwapChainContext) {
        res = mFSRReplacementFunctions.pOutQueuePresentKHR(_queue, &presentInfo);
    } else {
        res = vkQueuePresentKHR(_queue, &presentInfo);
    }
#else
    res = vkQueuePresentKHR(_queue, &presentInfo);
#endif

    if (res == VK_ERROR_OUT_OF_DATE_KHR) {
        recreateSwapChain(_windowWidth, _windowHeight);
    } else {
        CHECK_VULKAN_ERROR("vkQueuePresentKHR error %d\n", res);
    }

    // Advance frame index
    m_currentFrame = ++m_currentFrame % MAX_FRAMES_IN_FLIGHT;

    endTime = std::chrono::high_resolution_clock::now();
    deltaTime = std::chrono::duration<float, std::chrono::milliseconds::period>(endTime - startTime).count();
    startTime = endTime;

    return ret_status;
}
// TO DO common code getting into functions
void VulkanRenderer::createRenderPass() {
    // Array of our subpasses
    std::array<VkSubpassDescription, 3> subpasses{};

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

    VkAttachmentDescription colorAttachmentSSAO = colorAttachment;
    colorAttachmentSSAO.format = _ssaoBuffer.colorFormat;

    VkAttachmentDescription colorAttachmentShading = colorAttachment;
    colorAttachmentShading.format = _shadingBuffer.colorFormat;
    colorAttachmentShading.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkAttachmentDescription hdrBloomAttachment = colorAttachment;
    hdrBloomAttachment.format = _bloomBuffer[0].colorFormat;
    hdrBloomAttachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkAttachmentDescription gPassNormalAttachment = colorAttachment;
    gPassNormalAttachment.format = _gPassBuffer.normal.colorFormat;
    gPassNormalAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;  // not needed after subpasses completed, for performance
    gPassNormalAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VkAttachmentDescription gPassColorAttachment = gPassNormalAttachment;
    gPassColorAttachment.format = _gPassBuffer.color.colorFormat;
    gPassColorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

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
    shadowMapAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

    VkAttachmentDescription depthSSAOReadyAttachment =
        depthAttachment;  // already initialized depth texture from early renderPass
    depthSSAOReadyAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    depthSSAOReadyAttachment.initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    depthSSAOReadyAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

    VkAttachmentDescription shadowMapLoadAttachment = depthSSAOReadyAttachment;
    
    // Footprint uses a different depth format (_footprintBuffer.depthFormat)
    VkAttachmentDescription footPrintLoadAttachment = depthSSAOReadyAttachment;
    footPrintLoadAttachment.format = _footprintBuffer.depthFormat;
    footPrintLoadAttachment.initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    footPrintLoadAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

    VkAttachmentDescription viewSpacePosAttachment = colorAttachment;
    viewSpacePosAttachment.format = _viewSpaceBuffer.colorFormat;
    viewSpacePosAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    viewSpacePosAttachment.initialLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    viewSpacePosAttachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL; // Must stay readable for SSAO

    VkAttachmentDescription motionVecAttachment = colorAttachment;
    motionVecAttachment.format = _motionVectorsBuffer.colorFormat;
    motionVecAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    // from UNDEFINED to COLOR_ATTACHMENT_OPTIMAL in the first Depth PASS+
    motionVecAttachment.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    motionVecAttachment.format = _motionVectorsBuffer.colorFormat;
    // we continue writting in motion vectors buffer in the next pass (semi transparent objects)
    motionVecAttachment.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference colorAttachmentRef{};
    colorAttachmentRef.attachment = 0;
    colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference gPassNormalAttachmentRef = colorAttachmentRef;
    gPassNormalAttachmentRef.attachment = 1;

    VkAttachmentReference gPassColorAttachmentRef = colorAttachmentRef;
    gPassColorAttachmentRef.attachment = 2;

    VkAttachmentReference motionVecColorAttachmentRef = colorAttachmentRef;
    motionVecColorAttachmentRef.attachment = 11;

    VkAttachmentReference depthAttachmentRef{};
    depthAttachmentRef.attachment = 7;  // temporary depth buffer needed only for correct geometry output in g-pass
    depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference footPrintAttachmentRef{};
    footPrintAttachmentRef.attachment = 8;  // depthbuf with trails
    footPrintAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL; // Match descriptor expectation

    std::array<VkAttachmentReference, 4> gPassAttachment{colorAttachmentRef, gPassNormalAttachmentRef, gPassColorAttachmentRef,
                                                         motionVecColorAttachmentRef};

    subpasses[0].pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpasses[0].colorAttachmentCount = gPassAttachment.size();
    subpasses[0].pColorAttachments = &gPassAttachment[0];
    subpasses[0].pDepthStencilAttachment = &depthAttachmentRef;
    subpasses[0].inputAttachmentCount = 1u;
    subpasses[0].pInputAttachments = &footPrintAttachmentRef;

    // Set up Subpass 2 (SSAO)

    // References to attachments that subpass will take input from
    // NOTE: SSAO shader reads normal as subpassInput and depth/viewspace as sampler2D.
    // Keep only the true subpass input attachment here.
    std::array<VkAttachmentReference, 1> inputSSAOReferences;
    inputSSAOReferences[0].attachment = 1;
    inputSSAOReferences[0].layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkAttachmentReference colorAttachmentSSAOReference{};
    colorAttachmentSSAOReference.attachment = 3;
    colorAttachmentSSAOReference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    subpasses[1].pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpasses[1].colorAttachmentCount = 1;
    subpasses[1].pColorAttachments = &colorAttachmentSSAOReference;
    subpasses[1].inputAttachmentCount = static_cast<uint32_t>(inputSSAOReferences.size());
    subpasses[1].pInputAttachments = inputSSAOReferences.data();

    // Set up Subpass 3 (lighting)

    // References to attachments that subpass will take input from
    // NOTE: Lighting shader reads normal/color/ssao as subpassInput and depth/shadow via sampler2D.
    // Keep only the true subpass input attachments here.
    std::array<VkAttachmentReference, 3> inputReferences;
    inputReferences[0].attachment = 1;
    inputReferences[0].layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    inputReferences[1].attachment = 2;
    inputReferences[1].layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    inputReferences[2].attachment = 3;
    inputReferences[2].layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;  // SSAO

    VkAttachmentReference hdrAttachmentRef{};
    hdrAttachmentRef.attachment = 6;
    hdrAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference shadingAttachmentRef{};
    shadingAttachmentRef.attachment = 9;
    shadingAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference viewSpacePosAttachmentRef{};
    viewSpacePosAttachmentRef.attachment = 10;
    viewSpacePosAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    std::array<VkAttachmentReference, 3u> colorAndHdrAttachmentRef{colorAttachmentRef, hdrAttachmentRef, shadingAttachmentRef};
    subpasses[2].pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpasses[2].colorAttachmentCount = colorAndHdrAttachmentRef.size();
    subpasses[2].pColorAttachments = colorAndHdrAttachmentRef.data();
    subpasses[2].inputAttachmentCount = static_cast<uint32_t>(inputReferences.size());
    subpasses[2].pInputAttachments = inputReferences.data();

    // SUBPASS DEPENDENCIES

    // Need to determine when layout transitions occur using subpass dependencies
    std::array<VkSubpassDependency, 5> subpassDependencies{};

    // for color buffer from VK_IMAGE_LAYOUT_UNDEFINED to VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
    subpassDependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    subpassDependencies[0].dstSubpass = 0;
    subpassDependencies[0].srcStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    subpassDependencies[0].srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
    subpassDependencies[0].dstStageMask =
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    subpassDependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                                           VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                                           VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    subpassDependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

    // Subpass 1 layout (color/depth) to Subpass 2 layout (shader read)
    subpassDependencies[1].srcSubpass = 0;
    subpassDependencies[1].srcStageMask =
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    subpassDependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    subpassDependencies[1].dstSubpass = 1;
    subpassDependencies[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    subpassDependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    subpassDependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

    // Subpass 2 layout (color/depth) to Subpass 3 layout (shader read)
    subpassDependencies[2].srcSubpass = 1;
    subpassDependencies[2].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    subpassDependencies[2].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    subpassDependencies[2].dstSubpass = 2;
    subpassDependencies[2].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    subpassDependencies[2].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    subpassDependencies[2].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

    subpassDependencies[3].srcSubpass = 0;
    subpassDependencies[3].srcStageMask =
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    subpassDependencies[3].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    subpassDependencies[3].dstSubpass = 2;
    subpassDependencies[3].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    subpassDependencies[3].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    subpassDependencies[3].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

    // Conversion from VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL to VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
    // Transition must happen after...
    subpassDependencies[4].srcSubpass = 2;
    subpassDependencies[4].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    subpassDependencies[4].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    // But must happen before...
    subpassDependencies[4].dstSubpass = VK_SUBPASS_EXTERNAL;
    subpassDependencies[4].dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    subpassDependencies[4].dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
    subpassDependencies[4].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

    // temporary depth buffer for correct geometry output in g-Pass since depthAttachment is already formed before g-pass started
    VkAttachmentDescription depthTemporaryAttachment = depthAttachment;

    // colorBuffer starts in SHADER_READ_ONLY_OPTIMAL after FXAA on every frame after the first.
    // Declaring this explicitly avoids the UNDEFINED→COLOR_ATTACHMENT implicit transition
    // that AMD handles incorrectly when the actual layout is SHADER_READ_ONLY_OPTIMAL.
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    std::array<VkAttachmentDescription, 12> renderPassAttachments = {
        colorAttachment,          gPassNormalAttachment,   gPassColorAttachment,  colorAttachmentSSAO,
        depthSSAOReadyAttachment, shadowMapLoadAttachment, hdrBloomAttachment,    depthTemporaryAttachment,
        footPrintLoadAttachment,  colorAttachmentShading,  viewSpacePosAttachment, motionVecAttachment};

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
    colorAttachmentSemiTrans.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttachmentSemiTrans.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentDescription depthAttachmentSemiTrans = depthAttachment;
    depthAttachmentSemiTrans.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    depthAttachmentSemiTrans.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthAttachmentSemiTrans.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachmentSemiTrans.initialLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    depthAttachmentSemiTrans.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference colorAttachmentSemiTransReference{};
    colorAttachmentSemiTransReference.attachment = 0;
    colorAttachmentSemiTransReference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentDescription motionVectorsAttachmentSemiTrans = colorAttachmentSemiTrans;
    motionVectorsAttachmentSemiTrans.format = _motionVectorsBuffer.colorFormat;

    VkAttachmentReference motionVectorsAttachmentSemiTransReference{};
    motionVectorsAttachmentSemiTransReference.attachment = 2;
    motionVectorsAttachmentSemiTransReference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depthAttachmentSemiTransReference{};
    depthAttachmentSemiTransReference.attachment = 1;
    depthAttachmentSemiTransReference.layout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;

    std::array<VkAttachmentReference, 2> colorAttachmentsSemiTrans = {
        colorAttachmentSemiTransReference, motionVectorsAttachmentSemiTransReference};

    VkSubpassDescription subpassSemiTrans{};
    subpassSemiTrans.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpassSemiTrans.colorAttachmentCount = static_cast<uint32_t>(colorAttachmentsSemiTrans.size());
    subpassSemiTrans.pColorAttachments = colorAttachmentsSemiTrans.data();
    subpassSemiTrans.inputAttachmentCount = 0;
    subpassSemiTrans.pDepthStencilAttachment = &depthAttachmentSemiTransReference;

    std::array<VkAttachmentDescription, 3> renderPassAttachmentsSemiTrans = {
        colorAttachmentSemiTrans, depthAttachmentSemiTrans, motionVectorsAttachmentSemiTrans};

    std::array<VkSubpassDependency, 2u> dependencySemiTrans{};
    // color dependancy
    dependencySemiTrans[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    dependencySemiTrans[0].dstSubpass = 0;
    dependencySemiTrans[0].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependencySemiTrans[0].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dependencySemiTrans[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependencySemiTrans[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    // depth dependency (depth attachment must be ready for test/write)
    dependencySemiTrans[1].srcSubpass = VK_SUBPASS_EXTERNAL;
    dependencySemiTrans[1].dstSubpass = 0;
    dependencySemiTrans[1].srcStageMask = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    dependencySemiTrans[1].srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dependencySemiTrans[1].dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependencySemiTrans[1].dstAccessMask =
        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo renderPassCreateInfoSemiTrans = {};
    renderPassCreateInfoSemiTrans.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassCreateInfoSemiTrans.attachmentCount = static_cast<uint32_t>(renderPassAttachmentsSemiTrans.size());
    renderPassCreateInfoSemiTrans.pAttachments = renderPassAttachmentsSemiTrans.data();
    renderPassCreateInfoSemiTrans.subpassCount = 1;
    renderPassCreateInfoSemiTrans.pSubpasses = &subpassSemiTrans;
    renderPassCreateInfoSemiTrans.dependencyCount = static_cast<uint32_t>(dependencySemiTrans.size());
    renderPassCreateInfoSemiTrans.pDependencies = dependencySemiTrans.data();

    res = vkCreateRenderPass(_core.getDevice(), &renderPassCreateInfoSemiTrans, nullptr, &m_renderPassSemiTrans);
    CHECK_VULKAN_ERROR("vkCreateRenderPass error %d\n", res);

    Utils::printLog(INFO_PARAM, "Created a render pass for SEMI-TRANSPARENT OBJECTS");

    //-------------------------------------------------------//
    // Create info for Render Pass Gauss X Blurring for Bloom effect
    // the first preinitialized hdr texture will be blured by gauss x axis and stored into the second hdr texture
    VkAttachmentDescription colorAttachment1BlurX = {};
    colorAttachment1BlurX.format = _bloomBuffer[0].colorFormat;
    colorAttachment1BlurX.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment1BlurX.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment1BlurX.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment1BlurX.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment1BlurX.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment1BlurX.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment1BlurX.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkAttachmentDescription colorAttachment2BlurX = colorAttachment1BlurX;
    colorAttachment2BlurX.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    colorAttachment2BlurX.initialLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    colorAttachment2BlurX.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkAttachmentReference bloomHdrAttachmentReferenceBlurX{};
    bloomHdrAttachmentReferenceBlurX.attachment = 0;
    bloomHdrAttachmentReferenceBlurX.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    // References to attachments that subpass will take input from
    VkAttachmentReference inputBloomHdrReferenceBlurX{};
    inputBloomHdrReferenceBlurX.attachment = 1;
    inputBloomHdrReferenceBlurX.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkSubpassDescription subpassBlurX{};
    subpassBlurX.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpassBlurX.colorAttachmentCount = 1;
    subpassBlurX.pColorAttachments = &bloomHdrAttachmentReferenceBlurX;
    subpassBlurX.inputAttachmentCount = 1;
    subpassBlurX.pInputAttachments = &inputBloomHdrReferenceBlurX;

    std::array<VkAttachmentDescription, 2> renderPassAttachmentsBlurX = {colorAttachment1BlurX, colorAttachment2BlurX};

    // Subpass dependencies for layout transitions
    std::array<VkSubpassDependency, 1u> dependencyBlurX{};

    dependencyBlurX[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    dependencyBlurX[0].dstSubpass = 0;
    dependencyBlurX[0].srcStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    dependencyBlurX[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dependencyBlurX[0].srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
    dependencyBlurX[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT;
    dependencyBlurX[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

    VkRenderPassCreateInfo renderPassCreateInfoBlurX = {};
    renderPassCreateInfoBlurX.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassCreateInfoBlurX.attachmentCount = static_cast<uint32_t>(renderPassAttachmentsBlurX.size());
    renderPassCreateInfoBlurX.pAttachments = renderPassAttachmentsBlurX.data();
    renderPassCreateInfoBlurX.subpassCount = 1;
    renderPassCreateInfoBlurX.pSubpasses = &subpassBlurX;
    renderPassCreateInfoBlurX.dependencyCount = dependencyBlurX.size();
    renderPassCreateInfoBlurX.pDependencies = dependencyBlurX.data();

    res = vkCreateRenderPass(_core.getDevice(), &renderPassCreateInfoBlurX, nullptr, &m_renderPassXBlur);
    CHECK_VULKAN_ERROR("vkCreateRenderPass error %d\n", res);

    Utils::printLog(INFO_PARAM, "Created a render pass gauss x blurring");

    //-------------------------------------------------------//
    // Create info for Render Pass Gauss Y Blurring for Bloom effect
    // the previously blurred hdr texture will be blurred by y axis and stored into another hdr texture
    VkAttachmentDescription colorAttachment1BlurY = {};
    colorAttachment1BlurY.format = _bloomBuffer[0].colorFormat;
    colorAttachment1BlurY.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment1BlurY.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment1BlurY.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment1BlurY.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment1BlurY.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment1BlurY.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment1BlurY.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkAttachmentDescription colorAttachment2BlurY = colorAttachment1BlurY;
    colorAttachment2BlurY.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    colorAttachment2BlurY.initialLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    colorAttachment2BlurY.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkAttachmentReference bloomHdrAttachmentReferenceBlurY{};
    bloomHdrAttachmentReferenceBlurY.attachment = 0;
    bloomHdrAttachmentReferenceBlurY.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    // References to attachments that subpass will take input from
    VkAttachmentReference inputBloomHdrReferenceBlurY{};
    inputBloomHdrReferenceBlurY.attachment = 1;
    inputBloomHdrReferenceBlurY.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkSubpassDescription subpassBlurY{};
    subpassBlurY.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpassBlurY.colorAttachmentCount = 1;
    subpassBlurY.pColorAttachments = &bloomHdrAttachmentReferenceBlurY;
    subpassBlurY.inputAttachmentCount = 1;
    subpassBlurY.pInputAttachments = &inputBloomHdrReferenceBlurY;

    std::array<VkAttachmentDescription, 2> renderPassAttachmentsBlurY = {colorAttachment1BlurY, colorAttachment2BlurY};

    // Subpass dependencies for layout transitions
    std::array<VkSubpassDependency, 1u> dependencyBlurY{};

    dependencyBlurY[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    dependencyBlurY[0].dstSubpass = 0;
    dependencyBlurY[0].srcStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    dependencyBlurY[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dependencyBlurY[0].srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
    dependencyBlurY[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT;
    dependencyBlurY[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

    VkRenderPassCreateInfo renderPassCreateInfoBlurY = {};
    renderPassCreateInfoBlurY.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassCreateInfoBlurY.attachmentCount = static_cast<uint32_t>(renderPassAttachmentsBlurY.size());
    renderPassCreateInfoBlurY.pAttachments = renderPassAttachmentsBlurY.data();
    renderPassCreateInfoBlurY.subpassCount = 1;
    renderPassCreateInfoBlurY.pSubpasses = &subpassBlurY;
    renderPassCreateInfoBlurY.dependencyCount = dependencyBlurY.size();
    renderPassCreateInfoBlurY.pDependencies = dependencyBlurY.data();

    res = vkCreateRenderPass(_core.getDevice(), &renderPassCreateInfoBlurY, nullptr, &m_renderPassYBlur);
    CHECK_VULKAN_ERROR("vkCreateRenderPass error %d\n", res);

    Utils::printLog(INFO_PARAM, "Created a render pass gauss y blurring");

    //-------------------------------------------------------//
    // Bloom effect
    // we use previously blurred hdr texture and main color output attachment
    VkAttachmentDescription colorAttachment1Bloom = {};
    colorAttachment1Bloom.format = _colorBuffer.colorFormat;
    colorAttachment1Bloom.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment1Bloom.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    colorAttachment1Bloom.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment1Bloom.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment1Bloom.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment1Bloom.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttachment1Bloom.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    VkAttachmentDescription colorAttachment2Bloom = colorAttachment1Bloom;
    colorAttachment2Bloom.format = _bloomBuffer[0].colorFormat;
    colorAttachment2Bloom.initialLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    colorAttachment2Bloom.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkAttachmentReference bloomHdrAttachmentReferenceBloom{};
    bloomHdrAttachmentReferenceBloom.attachment = 0;
    bloomHdrAttachmentReferenceBloom.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    // References to attachments that subpass will take input from
    VkAttachmentReference inputBloomHdrReferenceBloom{};
    inputBloomHdrReferenceBloom.attachment = 1;
    inputBloomHdrReferenceBloom.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkSubpassDescription subpassBloom{};
    subpassBloom.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpassBloom.colorAttachmentCount = 1;
    subpassBloom.pColorAttachments = &bloomHdrAttachmentReferenceBloom;
    subpassBloom.inputAttachmentCount = 1;
    subpassBloom.pInputAttachments = &inputBloomHdrReferenceBloom;

    std::array<VkAttachmentDescription, 2> renderPassAttachmentsBloom = {colorAttachment1Bloom, colorAttachment2Bloom};

    // Subpass dependencies for layout transitions
    std::array<VkSubpassDependency, 1u> dependencyBloom{};

    dependencyBloom[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    dependencyBloom[0].dstSubpass = 0;
    dependencyBloom[0].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependencyBloom[0].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dependencyBloom[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dependencyBloom[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT;

    VkRenderPassCreateInfo renderPassCreateInfoBloom = {};
    renderPassCreateInfoBloom.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassCreateInfoBloom.attachmentCount = static_cast<uint32_t>(renderPassAttachmentsBloom.size());
    renderPassCreateInfoBloom.pAttachments = renderPassAttachmentsBloom.data();
    renderPassCreateInfoBloom.subpassCount = 1;
    renderPassCreateInfoBloom.pSubpasses = &subpassBloom;
    renderPassCreateInfoBloom.dependencyCount = dependencyBloom.size();
    renderPassCreateInfoBloom.pDependencies = dependencyBloom.data();

    res = vkCreateRenderPass(_core.getDevice(), &renderPassCreateInfoBloom, nullptr, &m_renderPassBloom);
    CHECK_VULKAN_ERROR("vkCreateRenderPass error %d\n", res);

    Utils::printLog(INFO_PARAM, "Created a render pass for bloom");

    //-------------------------------------------------------//
    // Create info for Render Pass FXAA
    VkAttachmentDescription swapchainAttachmentFXAA = {};
    swapchainAttachmentFXAA.format = _core.getSurfaceFormat().format;
    swapchainAttachmentFXAA.samples = VK_SAMPLE_COUNT_1_BIT;
    swapchainAttachmentFXAA.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    swapchainAttachmentFXAA.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    swapchainAttachmentFXAA.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    swapchainAttachmentFXAA.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    swapchainAttachmentFXAA.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    swapchainAttachmentFXAA.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    // Attachment reference uses an attachment index that refers to index in the attachment list passed to renderPassCreateInfo
    VkAttachmentReference swapchainColorAttachmentReferenceFXAA{};
    swapchainColorAttachmentReferenceFXAA.attachment = 0;
    swapchainColorAttachmentReferenceFXAA.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpassFXAA{};
    subpassFXAA.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpassFXAA.colorAttachmentCount = 1;
    subpassFXAA.pColorAttachments = &swapchainColorAttachmentReferenceFXAA;
    subpassFXAA.inputAttachmentCount = 0;
    subpassFXAA.pInputAttachments = nullptr;

    std::array<VkAttachmentDescription, 1> renderPassAttachmentsFXAA = {swapchainAttachmentFXAA};

    // Subpass dependencies for layout transitions
    VkSubpassDependency dependencyFXAA{};

    dependencyFXAA.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependencyFXAA.dstSubpass = 0;
    dependencyFXAA.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependencyFXAA.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependencyFXAA.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dependencyFXAA.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
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
    // Create info for Render Pass UI overlay (swapchain load + present)
    VkAttachmentDescription swapchainAttachmentUI = swapchainAttachmentFXAA;
    swapchainAttachmentUI.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    swapchainAttachmentUI.initialLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    swapchainAttachmentUI.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference swapchainColorAttachmentReferenceUI{};
    swapchainColorAttachmentReferenceUI.attachment = 0;
    swapchainColorAttachmentReferenceUI.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpassUI{};
    subpassUI.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpassUI.colorAttachmentCount = 1;
    subpassUI.pColorAttachments = &swapchainColorAttachmentReferenceUI;

    VkSubpassDependency dependencyUI{};
    dependencyUI.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependencyUI.dstSubpass = 0;
    dependencyUI.srcStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    dependencyUI.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
    dependencyUI.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependencyUI.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dependencyUI.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

    VkSubpassDependency dependencyUIToExternal{};
    dependencyUIToExternal.srcSubpass = 0;
    dependencyUIToExternal.dstSubpass = VK_SUBPASS_EXTERNAL;
    dependencyUIToExternal.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependencyUIToExternal.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dependencyUIToExternal.dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    dependencyUIToExternal.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
    dependencyUIToExternal.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

    std::array<VkAttachmentDescription, 1> renderPassAttachmentsUI = {swapchainAttachmentUI};

    VkRenderPassCreateInfo renderPassCreateInfoUI = {};
    renderPassCreateInfoUI.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassCreateInfoUI.attachmentCount = static_cast<uint32_t>(renderPassAttachmentsUI.size());
    renderPassCreateInfoUI.pAttachments = renderPassAttachmentsUI.data();
    renderPassCreateInfoUI.subpassCount = 1;
    renderPassCreateInfoUI.pSubpasses = &subpassUI;
    std::array<VkSubpassDependency, 2> uiDependencies = {dependencyUI, dependencyUIToExternal};
    renderPassCreateInfoUI.dependencyCount = static_cast<uint32_t>(uiDependencies.size());
    renderPassCreateInfoUI.pDependencies = uiDependencies.data();

    res = vkCreateRenderPass(_core.getDevice(), &renderPassCreateInfoUI, nullptr, &m_renderPassUIOverlay);
    CHECK_VULKAN_ERROR("vkCreateRenderPass error %d\n", res);

    Utils::printLog(INFO_PARAM, "Created a render pass UI overlay");

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
    VkSubpassDependency dependency{};

    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;  // store previous 'clear' operation
    // layout transition happens here from depth 'clear'
    dependency.dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;  // 'clear' writes to depth buffer
    dependency.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
                               VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;  // also we read 'cleared' depth buffer
    dependency.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

    VkRenderPassCreateInfo renderPassCreateInfoShadowMap = {};
    renderPassCreateInfoShadowMap.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassCreateInfoShadowMap.attachmentCount = 1;
    renderPassCreateInfoShadowMap.pAttachments = &shadowMapAttachment;
    renderPassCreateInfoShadowMap.subpassCount = 1;
    renderPassCreateInfoShadowMap.pSubpasses = &subpassesShadowMap;
    renderPassCreateInfoShadowMap.dependencyCount = 1;
    renderPassCreateInfoShadowMap.pDependencies = &dependency;

    res = vkCreateRenderPass(_core.getDevice(), &renderPassCreateInfoShadowMap, nullptr, &m_renderPassShadowMap);
    CHECK_VULKAN_ERROR("vkCreateRenderPass error %d\n", res);

    Utils::printLog(INFO_PARAM, "Created a render pass SHADOW MAP");

    //-------------------------------------------------------//
    // Create info for Render Pass DEPTH writing and viewSpace positions for SSAO
    VkAttachmentReference depthAttachmentForSSAORef{};
    depthAttachmentForSSAORef.attachment = 0;
    depthAttachmentForSSAORef.layout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;

    VkAttachmentReference viewSpacePosAttachmentForSSAORef{};
    viewSpacePosAttachmentForSSAORef.attachment = 1;
    viewSpacePosAttachmentForSSAORef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference motionVectorsAttachmentRef{};
    motionVectorsAttachmentRef.attachment = 2;
    motionVectorsAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    std::array<VkAttachmentReference, 2u> colorAttachmentRefForDepthWritingPlus{viewSpacePosAttachmentForSSAORef,
                                                                                motionVectorsAttachmentRef};

    // actually it's fully independant pass right now
    VkSubpassDescription subpassesDepthSSAO{};
    subpassesDepthSSAO.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpassesDepthSSAO.colorAttachmentCount = static_cast<uint32_t>(colorAttachmentRefForDepthWritingPlus.size());
    subpassesDepthSSAO.pColorAttachments = colorAttachmentRefForDepthWritingPlus.data();
    subpassesDepthSSAO.pDepthStencilAttachment = &depthAttachmentForSSAORef;

    // Subpass dependencies for layout transitions
    std::array<VkSubpassDependency, 3u> dependencyDepthAndViewSpacePosForSSAO{};

    // depth
    dependencyDepthAndViewSpacePosForSSAO[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    dependencyDepthAndViewSpacePosForSSAO[0].dstSubpass = 0;
    dependencyDepthAndViewSpacePosForSSAO[0].srcStageMask =
        VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;  // previous 'clear' operation
    // layout transition happens here from depth 'clear'
    dependencyDepthAndViewSpacePosForSSAO[0].dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependencyDepthAndViewSpacePosForSSAO[0].srcAccessMask =
        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;  // 'clear' writes to depth buffer
    dependencyDepthAndViewSpacePosForSSAO[0].dstAccessMask =
        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;  // also we read 'cleared' depth buffer
    dependencyDepthAndViewSpacePosForSSAO[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

    // viewspace pos color
    dependencyDepthAndViewSpacePosForSSAO[1].srcSubpass = VK_SUBPASS_EXTERNAL;
    dependencyDepthAndViewSpacePosForSSAO[1].dstSubpass = 0;
    dependencyDepthAndViewSpacePosForSSAO[1].srcStageMask =
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;  // store previous 'clear' operation
    dependencyDepthAndViewSpacePosForSSAO[1].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependencyDepthAndViewSpacePosForSSAO[1].srcAccessMask =
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;  // 'clear' writes to color buffer
    dependencyDepthAndViewSpacePosForSSAO[1].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dependencyDepthAndViewSpacePosForSSAO[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

    // motion vectors buf for DLAA/FSR
    dependencyDepthAndViewSpacePosForSSAO[2].srcSubpass = VK_SUBPASS_EXTERNAL;
    dependencyDepthAndViewSpacePosForSSAO[2].dstSubpass = 0;
    dependencyDepthAndViewSpacePosForSSAO[2].srcStageMask =
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;  // store previous 'clear' operation
    dependencyDepthAndViewSpacePosForSSAO[2].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependencyDepthAndViewSpacePosForSSAO[2].srcAccessMask =
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;  // 'clear' writes to color buffer
    dependencyDepthAndViewSpacePosForSSAO[2].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dependencyDepthAndViewSpacePosForSSAO[2].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

    VkAttachmentDescription colorAttachmentViewSpacePos = colorAttachment;
    colorAttachmentViewSpacePos.initialLayout =
        VK_IMAGE_LAYOUT_UNDEFINED;  // it automatically  transits to COLOR_ATTACHMENT_OPTIMAL when render pass starts
    colorAttachmentViewSpacePos.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    colorAttachmentViewSpacePos.format = _viewSpaceBuffer.colorFormat;
    VkAttachmentDescription colorAttachmentMotionVectors = colorAttachmentViewSpacePos;
    colorAttachmentMotionVectors.format = _motionVectorsBuffer.colorFormat;
    // we continue writting in motion vectors buffer in the next pass (semi transparent objects)
    colorAttachmentMotionVectors.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    VkAttachmentDescription depthAttachmentForSSAO = depthAttachment;
    depthAttachmentForSSAO.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    std::array<VkAttachmentDescription, 3> renderPassAttachmentsSSAO = {depthAttachmentForSSAO, colorAttachmentViewSpacePos, colorAttachmentMotionVectors};

    VkRenderPassCreateInfo renderPassCreateInfoDepthForSSAO = {};
    renderPassCreateInfoDepthForSSAO.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassCreateInfoDepthForSSAO.attachmentCount = static_cast<uint32_t>(renderPassAttachmentsSSAO.size());
    renderPassCreateInfoDepthForSSAO.pAttachments = renderPassAttachmentsSSAO.data();
    renderPassCreateInfoDepthForSSAO.subpassCount = 1;
    renderPassCreateInfoDepthForSSAO.pSubpasses = &subpassesDepthSSAO;
    renderPassCreateInfoDepthForSSAO.dependencyCount = dependencyDepthAndViewSpacePosForSSAO.size();
    renderPassCreateInfoDepthForSSAO.pDependencies = dependencyDepthAndViewSpacePosForSSAO.data();

    res = vkCreateRenderPass(_core.getDevice(), &renderPassCreateInfoDepthForSSAO, nullptr, &m_renderPassDepth);
    CHECK_VULKAN_ERROR("vkCreateRenderPass error %d\n", res);

    Utils::printLog(INFO_PARAM, "Created a render pass depth");

    //-------------------------------------------------------//
    // Create info for Render Pass: FootPrint effect
    VkAttachmentDescription depthAttachmentFootPrint = depthAttachment;
    depthAttachmentFootPrint.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;  // we accumulate trails of the vehicle
    depthAttachmentFootPrint.format = _footprintBuffer.depthFormat;
    depthAttachmentFootPrint.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depthAttachmentFootPrint.initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    depthAttachmentFootPrint.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    std::array<VkAttachmentDescription, 1> renderPassAttachmentsFootPrint = {depthAttachmentFootPrint};

    VkAttachmentReference footPrintDepthAttachmentReferences = {};
    footPrintDepthAttachmentReferences.attachment = 0;
    footPrintDepthAttachmentReferences.layout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpassFootPrint{};
    subpassFootPrint.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpassFootPrint.colorAttachmentCount = 0u;
    subpassFootPrint.pColorAttachments = VK_NULL_HANDLE;
    subpassFootPrint.pDepthStencilAttachment = &footPrintDepthAttachmentReferences;
    subpassFootPrint.inputAttachmentCount = 0u;

    // Subpass dependencies for layout transitions
    VkSubpassDependency dependencyFootPrint{};

    dependencyFootPrint.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependencyFootPrint.dstSubpass = 0;
    dependencyFootPrint.srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_TESSELLATION_EVALUATION_SHADER_BIT |
                                       VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    dependencyFootPrint.srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dependencyFootPrint.dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependencyFootPrint.dstAccessMask =
        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;

    VkRenderPassCreateInfo renderPassCreateInfoFootPrint = {};
    renderPassCreateInfoFootPrint.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassCreateInfoFootPrint.attachmentCount = static_cast<uint32_t>(renderPassAttachmentsFootPrint.size());
    renderPassCreateInfoFootPrint.pAttachments = renderPassAttachmentsFootPrint.data();
    renderPassCreateInfoFootPrint.subpassCount = 1;
    renderPassCreateInfoFootPrint.pSubpasses = &subpassFootPrint;
    renderPassCreateInfoFootPrint.dependencyCount = 1u;
    renderPassCreateInfoFootPrint.pDependencies = &dependencyFootPrint;

    res = vkCreateRenderPass(_core.getDevice(), &renderPassCreateInfoFootPrint, nullptr, &m_renderPassFootprint);
    CHECK_VULKAN_ERROR("vkCreateRenderPass error %d\n", res);

    Utils::printLog(INFO_PARAM, "Created a render pass for footprint effect");

    //-------------------------------------------------------//
    // SSAO blurring & applying
    // we use previously prepared noisy SSAO passthough texture _shadingBuffer to blur and apply
    VkAttachmentDescription colorAttachmentSSAOblurOutput = {};  // main swapChain buffer for applying
    colorAttachmentSSAOblurOutput.format = _colorBuffer.colorFormat;
    colorAttachmentSSAOblurOutput.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachmentSSAOblurOutput.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    colorAttachmentSSAOblurOutput.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachmentSSAOblurOutput.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachmentSSAOblurOutput.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachmentSSAOblurOutput.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttachmentSSAOblurOutput.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    VkAttachmentDescription colorAttachmentSSAOblurInput = colorAttachmentSSAOblurOutput;  // SSAO input for blurring
    colorAttachmentSSAOblurInput.format = _shadingBuffer.colorFormat;
    colorAttachmentSSAOblurInput.initialLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    colorAttachmentSSAOblurInput.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkAttachmentReference attachmentReferenceSSAOblurOutput{};
    attachmentReferenceSSAOblurOutput.attachment = 0;
    attachmentReferenceSSAOblurOutput.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference attachmentReferenceSSAOblurInput{};
    attachmentReferenceSSAOblurInput.attachment = 1;
    attachmentReferenceSSAOblurInput.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkSubpassDescription subpassSSAOblur{};
    subpassSSAOblur.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpassSSAOblur.colorAttachmentCount = 1;
    subpassSSAOblur.pColorAttachments = &attachmentReferenceSSAOblurOutput;
    subpassSSAOblur.inputAttachmentCount = 1;
    subpassSSAOblur.pInputAttachments = &attachmentReferenceSSAOblurInput;

    std::array<VkAttachmentDescription, 2> renderPassAttachmentsSSAOblur = {colorAttachmentSSAOblurOutput,
                                                                            colorAttachmentSSAOblurInput};

    // Subpass dependencies for layout transitions
    std::array<VkSubpassDependency, 1u> dependencySSAOblur{};

    dependencySSAOblur[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    dependencySSAOblur[0].dstSubpass = 0;
    dependencySSAOblur[0].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependencySSAOblur[0].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dependencySSAOblur[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dependencySSAOblur[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT;

    VkRenderPassCreateInfo renderPassCreateInfoSSAOblur = {};
    renderPassCreateInfoSSAOblur.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassCreateInfoSSAOblur.attachmentCount = static_cast<uint32_t>(renderPassAttachmentsSSAOblur.size());
    renderPassCreateInfoSSAOblur.pAttachments = renderPassAttachmentsSSAOblur.data();
    renderPassCreateInfoSSAOblur.subpassCount = 1;
    renderPassCreateInfoSSAOblur.pSubpasses = &subpassSSAOblur;
    renderPassCreateInfoSSAOblur.dependencyCount = dependencySSAOblur.size();
    renderPassCreateInfoSSAOblur.pDependencies = dependencySSAOblur.data();

    res = vkCreateRenderPass(_core.getDevice(), &renderPassCreateInfoSSAOblur, nullptr, &m_renderPassSSAOblur);
    CHECK_VULKAN_ERROR("vkCreateRenderPass error %d\n", res);

    Utils::printLog(INFO_PARAM, "Created a render pass for ssao blur");
}

void VulkanRenderer::createFramebuffer() {
    VkResult res;
    for (size_t i = 0; i < _swapChain.images.size(); i++) {
        std::array<VkImageView, 12> attachments = {_colorBuffer.colorBufferImageView[i],
                                                   _gPassBuffer.normal.colorBufferImageView[i],
                                                   _gPassBuffer.color.colorBufferImageView[i],
                                                   _ssaoBuffer.colorBufferImageView[i],
                                                   _depthBuffer.depthImageView,
                                                   _shadowMapBuffer.depthImageView,
                                                   _bloomBuffer[0].colorBufferImageView[i],
                                                   _depthTempBuffer.depthImageView,
                                                   _footprintBuffer.depthImageView,
                                                   _shadingBuffer.colorBufferImageView[i],
                                                   _viewSpaceBuffer.colorBufferImageView[i],
                                                   _motionVectorsBuffer.colorBufferImageView[i]};

        // The color attachment differs for every swap chain image,
        // but the same depth image can be used by all of them
        // because only a single subpass is running at the same time due to our semaphores.

        VkFramebufferCreateInfo fbCreateInfo = {};
        fbCreateInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbCreateInfo.renderPass = m_renderPass;
        fbCreateInfo.attachmentCount = attachments.size();
        fbCreateInfo.pAttachments = attachments.data();
        fbCreateInfo.width = _offscreenWidth;
        fbCreateInfo.height = _offscreenHeight;
        fbCreateInfo.layers = 1;

        res = vkCreateFramebuffer(_core.getDevice(), &fbCreateInfo, nullptr, &m_fbs[i]);
        CHECK_VULKAN_ERROR("vkCreateFramebuffer error %d\n", res);
    }

    //-------------------------------------------------------//
    // FBO Gauss x blurring for bloom effect
    for (size_t i = 0; i < _swapChain.images.size(); i++) {
        std::array<VkImageView, 2> attachments = {_bloomBuffer[1].colorBufferImageView[i],
                                                  _bloomBuffer[0].colorBufferImageView[i]};

        VkFramebufferCreateInfo fbCreateInfo = {};
        fbCreateInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbCreateInfo.renderPass = m_renderPassXBlur;
        fbCreateInfo.attachmentCount = attachments.size();
        fbCreateInfo.pAttachments = attachments.data();
        fbCreateInfo.width = _offscreenWidth;
        fbCreateInfo.height = _offscreenHeight;
        fbCreateInfo.layers = 1;

        res = vkCreateFramebuffer(_core.getDevice(), &fbCreateInfo, nullptr, &m_fbsXBlur[i]);
        CHECK_VULKAN_ERROR("vkCreateFramebuffer Gauss x blurring error %d\n", res);
    }

    //-------------------------------------------------------//
    // FBO Gauss y blurring for bloom effect
    for (size_t i = 0; i < _swapChain.images.size(); i++) {
        std::array<VkImageView, 2> attachments = {_bloomBuffer[0].colorBufferImageView[i],
                                                  _bloomBuffer[1].colorBufferImageView[i]};

        VkFramebufferCreateInfo fbCreateInfo = {};
        fbCreateInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbCreateInfo.renderPass = m_renderPassYBlur;
        fbCreateInfo.attachmentCount = attachments.size();
        fbCreateInfo.pAttachments = attachments.data();
        fbCreateInfo.width = _offscreenWidth;
        fbCreateInfo.height = _offscreenHeight;
        fbCreateInfo.layers = 1;

        res = vkCreateFramebuffer(_core.getDevice(), &fbCreateInfo, nullptr, &m_fbsYBlur[i]);
        CHECK_VULKAN_ERROR("vkCreateFramebuffer Gauss y blurring error %d\n", res);
    }

    //-------------------------------------------------------//
    // FBO for bloom effect
    for (size_t i = 0; i < _swapChain.images.size(); i++) {
        std::array<VkImageView, 2> attachments = {_colorBuffer.colorBufferImageView[i], _bloomBuffer[0].colorBufferImageView[i]};

        VkFramebufferCreateInfo fbCreateInfo = {};
        fbCreateInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbCreateInfo.renderPass = m_renderPassBloom;
        fbCreateInfo.attachmentCount = attachments.size();
        fbCreateInfo.pAttachments = attachments.data();
        fbCreateInfo.width = _offscreenWidth;
        fbCreateInfo.height = _offscreenHeight;
        fbCreateInfo.layers = 1;

        res = vkCreateFramebuffer(_core.getDevice(), &fbCreateInfo, nullptr, &m_fbsBloom[i]);
        CHECK_VULKAN_ERROR("vkCreateFramebuffer Bloom error %d\n", res);
    }

    //-------------------------------------------------------//
    // FBO FXAA
    for (size_t i = 0; i < _swapChain.images.size(); i++) {
        if (Utils::VulkanCreateImageView(_core.getDevice(), _swapChain.images[i], _core.getSurfaceFormat().format,
                                         VK_IMAGE_ASPECT_COLOR_BIT, _swapChain.views[i]) != VK_SUCCESS) {
            Utils::printLog(ERROR_PARAM, "failed to create texture image view!");
        }

        std::array<VkImageView, 1> attachments = {_swapChain.views[i]};

        VkFramebufferCreateInfo fbCreateInfo = {};
        fbCreateInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbCreateInfo.renderPass = m_renderPassFXAA;
        fbCreateInfo.attachmentCount = attachments.size();
        fbCreateInfo.pAttachments = attachments.data();
        fbCreateInfo.width = _windowWidth;
        fbCreateInfo.height = _windowHeight;
        fbCreateInfo.layers = 1;

        res = vkCreateFramebuffer(_core.getDevice(), &fbCreateInfo, nullptr, &m_fbsFXAA[i]);
        CHECK_VULKAN_ERROR("vkCreateFramebuffer FXAA error %d\n", res);
    }

    //-------------------------------------------------------//
    // FBO UI overlay
    for (size_t i = 0; i < _swapChain.images.size(); i++) {
        std::array<VkImageView, 1> attachments = {_swapChain.views[i]};

        VkFramebufferCreateInfo fbCreateInfo = {};
        fbCreateInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbCreateInfo.renderPass = m_renderPassUIOverlay;
        fbCreateInfo.attachmentCount = attachments.size();
        fbCreateInfo.pAttachments = attachments.data();
        fbCreateInfo.width = _windowWidth;
        fbCreateInfo.height = _windowHeight;
        fbCreateInfo.layers = 1;

        res = vkCreateFramebuffer(_core.getDevice(), &fbCreateInfo, nullptr, &m_fbsUIOverlay[i]);
        CHECK_VULKAN_ERROR("vkCreateFramebuffer UI overlay error %d\n", res);
    }

    //-------------------------------------------------------//
    // FBO SEMI-TRANSPARENT OBJECTS
    for (size_t i = 0; i < _swapChain.images.size(); i++) {
        std::array<VkImageView, 3> attachments = {
            _colorBuffer.colorBufferImageView[i], _depthBuffer.depthImageView, _motionVectorsBuffer.colorBufferImageView[i]};

        VkFramebufferCreateInfo fbCreateInfo = {};
        fbCreateInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbCreateInfo.renderPass = m_renderPassSemiTrans;
        fbCreateInfo.attachmentCount = attachments.size();
        fbCreateInfo.pAttachments = attachments.data();
        fbCreateInfo.width = _offscreenWidth;
        fbCreateInfo.height = _offscreenHeight;
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
        fbCreateInfo.width = _shadowMapBuffer.width;
        fbCreateInfo.height = _shadowMapBuffer.height;
        fbCreateInfo.layers = 1;

        res = vkCreateFramebuffer(_core.getDevice(), &fbCreateInfo, nullptr, &m_fbsShadowMap[i]);
        CHECK_VULKAN_ERROR("vkCreateFramebuffer SHADOW MAP error %d\n", res);
    }

    //-------------------------------------------------------//
    // FBO DEPTH PASS AND VIEW SPACE POS (for SSAO) + MOTION VECTORS BUF (for DLAA)
    for (size_t i = 0; i < _swapChain.images.size(); i++) {
        std::array<VkImageView, 3> attachments = {_depthBuffer.depthImageView, _viewSpaceBuffer.colorBufferImageView[i],
            _motionVectorsBuffer.colorBufferImageView[i]};

        VkFramebufferCreateInfo fbCreateInfo = {};
        fbCreateInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbCreateInfo.renderPass = m_renderPassDepth;
        fbCreateInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
        fbCreateInfo.pAttachments = attachments.data();
        fbCreateInfo.width = _depthBuffer.width;
        fbCreateInfo.height = _depthBuffer.height;
        fbCreateInfo.layers = 1;

        res = vkCreateFramebuffer(_core.getDevice(), &fbCreateInfo, nullptr, &m_fbsDepth[i]);
        CHECK_VULKAN_ERROR("vkCreateFramebuffer DEPTH PASS error %d\n", res);
    }

    //-------------------------------------------------------//
    // FOOTPRINT PASS (for tire tracks)
    for (size_t i = 0; i < _swapChain.images.size(); i++) {
        VkImageView attachment = _footprintBuffer.depthImageView;

        VkFramebufferCreateInfo fbCreateInfo = {};
        fbCreateInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbCreateInfo.renderPass = m_renderPassFootprint;
        fbCreateInfo.attachmentCount = 1u;
        fbCreateInfo.pAttachments = &attachment;
        fbCreateInfo.width = _footprintBuffer.width;
        fbCreateInfo.height = _footprintBuffer.height;
        fbCreateInfo.layers = 1;

        res = vkCreateFramebuffer(_core.getDevice(), &fbCreateInfo, nullptr, &m_fbsFootprint[i]);
        CHECK_VULKAN_ERROR("vkCreateFramebuffer FOOTPRINT PASS error %d\n", res);
    }

    //-------------------------------------------------------//
    // FBO for SSAO blurring and applying
    for (size_t i = 0; i < _swapChain.images.size(); i++) {
        std::array<VkImageView, 2> attachments = {_colorBuffer.colorBufferImageView[i], _shadingBuffer.colorBufferImageView[i]};

        VkFramebufferCreateInfo fbCreateInfo = {};
        fbCreateInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbCreateInfo.renderPass = m_renderPassSSAOblur;
        fbCreateInfo.attachmentCount = attachments.size();
        fbCreateInfo.pAttachments = attachments.data();
        fbCreateInfo.width = _offscreenWidth;
        fbCreateInfo.height = _offscreenHeight;
        fbCreateInfo.layers = 1;

        res = vkCreateFramebuffer(_core.getDevice(), &fbCreateInfo, nullptr, &m_fbsSSAOblur[i]);
        CHECK_VULKAN_ERROR("vkCreateFramebuffer Bloom error %d\n", res);
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

    for (size_t i = 0; i < _swapChain.images.size(); i++) {
        if (vkCreateSemaphore(_core.getDevice(), &semaphoreCreateInfo, nullptr, &m_presentCompleteSem[i]) != VK_SUCCESS ||
            vkCreateSemaphore(_core.getDevice(), &semaphoreCreateInfo, nullptr, &m_renderCompleteSem[i]) != VK_SUCCESS ||
            vkCreateFence(_core.getDevice(), &fenceCreateInfo, nullptr, &m_drawFences[i]) != VK_SUCCESS) {
            Utils::printLog(ERROR_PARAM, "Failed to create a Semaphore and/or Fence!");
        }
    }
}

void VulkanRenderer::createDescriptorPoolForImGui() {
    // descriptor pool for IMGUI
    // the size of the pool is very oversize
    VkDescriptorPoolSize pool_sizes[] = {{VK_DESCRIPTOR_TYPE_SAMPLER, 100},
                                         {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 100},
                                         {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 100},
                                         {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 100},
                                         {VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 100},
                                         {VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 100},
                                         {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 100},
                                         {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 100},
                                         {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 100},
                                         {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 100},
                                         {VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 100}};

    VkDescriptorPoolCreateInfo pool_info = {};
    pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    pool_info.maxSets = 100;
    pool_info.poolSizeCount = std::size(pool_sizes);
    pool_info.pPoolSizes = pool_sizes;

    auto res = vkCreateDescriptorPool(_core.getDevice(), &pool_info, nullptr, &mImguiPool);
    CHECK_VULKAN_ERROR("ImGui reation failed", res);

    // When ImGui Vulkan backend is built with VK_NO_PROTOTYPES (e.g. with volk),
    // backend-level function pointers must be loaded explicitly before Init.
    struct ImGuiVulkanLoaderData {
        VkInstance instance;
        VkDevice device;
    } loaderData{_core.getInstance(), _core.getDevice()};

    bool imguiVulkanFnsLoaded = ImGui_ImplVulkan_LoadFunctions(
        [](const char* function_name, void* user_data) -> PFN_vkVoidFunction {
            const auto* data = reinterpret_cast<const ImGuiVulkanLoaderData*>(user_data);

            // Resolve through instance first to avoid querying instance-level commands via vkGetDeviceProcAddr.
            PFN_vkVoidFunction fn = vkGetInstanceProcAddr(data->instance, function_name);
            if (fn) {
                return fn;
            }

            // Fallback for commands only exposed through the device dispatch table.
            return vkGetDeviceProcAddr(data->device, function_name);
        },
        &loaderData);
    if (!imguiVulkanFnsLoaded) {
        Utils::printLog(ERROR_PARAM, "ImGui_ImplVulkan_LoadFunctions failed");
        assert(false && "ImGui Vulkan function loading failed");
        return;
    }

    // this initializes imgui for Vulkan
    ImGui_ImplVulkan_InitInfo init_info = {};
    init_info.Instance = _core.getInstance();
    init_info.PhysicalDevice = _core.getPhysDevice();
    init_info.Device = _core.getDevice();
    init_info.Queue = _queue;
    init_info.DescriptorPool = mImguiPool;
    init_info.MinImageCount = static_cast<uint32_t>(_swapChain.images.size());
    init_info.ImageCount = static_cast<uint32_t>(_swapChain.images.size());
    init_info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    init_info.RenderPass = m_renderPassUIOverlay;

    ImGui_ImplVulkan_Init(&init_info);
}

void VulkanRenderer::createPipeline() {
    for (auto& pipelineCreator : m_pipelineCreators) {
        pipelineCreator->recreate();
    }
}

void VulkanRenderer::createDepthResources() {
    // 32bits depth is preferable
    if (!Utils::VulkanFindSupportedFormat(_core.getPhysDevice(), {VK_FORMAT_D24_UNORM_S8_UINT, VK_FORMAT_D32_SFLOAT},
                                          VK_IMAGE_TILING_OPTIMAL, VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT,
                                          _depthBuffer.depthFormat)) {
        Utils::printLog(ERROR_PARAM, "failed to find supported format!");
        return;
    }

    _shadowMapBuffer.depthFormat = _depthBuffer.depthFormat;
    _depthTempBuffer.depthFormat = _depthBuffer.depthFormat;

    // 16bits depth is preferable since is more than enough for footprint
    if (!Utils::VulkanFindSupportedFormat(_core.getPhysDevice(), {VK_FORMAT_D24_UNORM_S8_UINT, VK_FORMAT_D16_UNORM},
                                          VK_IMAGE_TILING_OPTIMAL, VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT,
                                          _footprintBuffer.depthFormat)) {
        Utils::printLog(ERROR_PARAM, "failed to find supported format!");
        return;
    }

    Utils::VulkanCreateImage(
        _core.getDevice(), _core.getPhysDevice(), _shadowMapBuffer.width, _shadowMapBuffer.height, _shadowMapBuffer.depthFormat,
        VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, _shadowMapBuffer.depthImage, _shadowMapBuffer.depthImageMemory);
    Utils::VulkanCreateImageView(_core.getDevice(), _shadowMapBuffer.depthImage, _shadowMapBuffer.depthFormat,
                                 VK_IMAGE_ASPECT_DEPTH_BIT, _shadowMapBuffer.depthImageView);

    Utils::VulkanCreateImage(
        _core.getDevice(), _core.getPhysDevice(), _depthBuffer.width, _depthBuffer.height, _depthBuffer.depthFormat,
        VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, _depthBuffer.depthImage, _depthBuffer.depthImageMemory);
    Utils::VulkanCreateImageView(_core.getDevice(), _depthBuffer.depthImage, _depthBuffer.depthFormat, VK_IMAGE_ASPECT_DEPTH_BIT,
                                 _depthBuffer.depthImageView);

    Utils::VulkanCreateImage(_core.getDevice(), _core.getPhysDevice(), _depthTempBuffer.width, _depthTempBuffer.height,
                             _depthTempBuffer.depthFormat, VK_IMAGE_TILING_OPTIMAL,
                             VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT,
                             VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, _depthTempBuffer.depthImage, _depthTempBuffer.depthImageMemory);
    Utils::VulkanCreateImageView(_core.getDevice(), _depthTempBuffer.depthImage, _depthTempBuffer.depthFormat,
                                 VK_IMAGE_ASPECT_DEPTH_BIT, _depthTempBuffer.depthImageView);

    Utils::VulkanCreateImage(_core.getDevice(), _core.getPhysDevice(), _footprintBuffer.width, _footprintBuffer.height,
                             _footprintBuffer.depthFormat, VK_IMAGE_TILING_OPTIMAL,
                             VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT |
                                 VK_IMAGE_USAGE_SAMPLED_BIT,
                             VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, _footprintBuffer.depthImage, _footprintBuffer.depthImageMemory);

    // Keep the footprint depth image in its steady sampled/read-only state between passes.
    Utils::VulkanTransitionImageLayout(_core.getDevice(), _queue, _cmdBufPool, _footprintBuffer.depthImage,
                                       _footprintBuffer.depthFormat, VK_IMAGE_LAYOUT_UNDEFINED,
                                       VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_DEPTH_BIT, 1U, 1U);

    Utils::VulkanCreateImageView(_core.getDevice(), _footprintBuffer.depthImage, _footprintBuffer.depthFormat,
                                 VK_IMAGE_ASPECT_DEPTH_BIT, _footprintBuffer.depthImageView);
}

#if defined(USE_DLSS) && USE_DLSS
void VulkanRenderer::setDLSSResourceTags(uint32_t currentImage, const sl::FrameToken& frameToken) {
    if (!_core.isDlssSupported()) {
        return;
    }

    sl::Resource depthRes(sl::ResourceType::eTex2d, _depthBuffer.depthImage, _depthBuffer.depthImageMemory,
                          _depthBuffer.depthImageView, static_cast<uint32_t>(VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL));
    depthRes.width = _depthBuffer.width;
    depthRes.height = _depthBuffer.height;
    depthRes.nativeFormat = static_cast<uint32_t>(_depthBuffer.depthFormat);

    sl::Resource colorRes(sl::ResourceType::eTex2d, _colorBuffer.colorBufferImage[currentImage],
                          _colorBuffer.colorBufferImageMemory[currentImage], _colorBuffer.colorBufferImageView[currentImage],
                          static_cast<uint32_t>(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL));
    colorRes.width = _offscreenWidth;
    colorRes.height = _offscreenHeight;
    colorRes.nativeFormat = static_cast<uint32_t>(_colorBuffer.colorFormat);

    sl::Resource motionRes(sl::ResourceType::eTex2d, _motionVectorsBuffer.colorBufferImage[currentImage],
                           _motionVectorsBuffer.colorBufferImageMemory[currentImage],
                           _motionVectorsBuffer.colorBufferImageView[currentImage],
                           static_cast<uint32_t>(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL));
    motionRes.width = _offscreenWidth;
    motionRes.height = _offscreenHeight;
    motionRes.nativeFormat = static_cast<uint32_t>(_motionVectorsBuffer.colorFormat);

    sl::Resource outputRes(sl::ResourceType::eTex2d, _dlssOutputBuffer.colorBufferImage[currentImage],
                           _dlssOutputBuffer.colorBufferImageMemory[currentImage], _dlssOutputBuffer.colorBufferImageView[currentImage],
                           static_cast<uint32_t>(VK_IMAGE_LAYOUT_GENERAL));
    outputRes.width = _windowWidth;
    outputRes.height = _windowHeight;
    outputRes.nativeFormat = static_cast<uint32_t>(_dlssOutputBuffer.colorFormat);

    sl::ResourceTag tags[] = {
        {&depthRes, sl::kBufferTypeDepth, sl::ResourceLifecycle::eValidUntilPresent},
        {&colorRes, sl::kBufferTypeScalingInputColor, sl::ResourceLifecycle::eValidUntilPresent},
        {&motionRes, sl::kBufferTypeMotionVectors, sl::ResourceLifecycle::eValidUntilPresent},
        {&outputRes, sl::kBufferTypeScalingOutputColor, sl::ResourceLifecycle::eValidUntilPresent},
    };

    static const sl::ViewportHandle viewport(0);
    sl::Result slRes = _core.slSetTagForFrameSafe(frameToken, viewport, tags, static_cast<uint32_t>(std::size(tags)),
                                                  reinterpret_cast<sl::CommandBuffer*>(_cmdBufs[currentImage]));
    if (slRes != sl::Result::eOk && !m_slTagErrorLogged) {
        Utils::printLog(INFO_PARAM, "slSetTagForFrame failed for DLSS resources, sl::Result=%d", static_cast<int>(slRes));
        m_slTagErrorLogged = true;
    }
}

void VulkanRenderer::setDLSSConstants(const sl::FrameToken& frameToken) {
    if (!_core.isDlssSupported()) {
        return;
    }

    // previous frame reprojection, to reproject current Pos to previous frame Pos in clip space
    const glm::mat4 clipToPrevClip = mViewProj.prevViewProj * glm::inverse(mViewProj.viewProj);
    // to reproject previous Pos to current frame Pos in clip space
    const glm::mat4 prevClipToClip = glm::inverse(clipToPrevClip);
    const glm::mat4 clipToCameraView = glm::inverse(mViewProj.proj);
    const glm::mat4 invView = glm::inverse(mViewProj.view);

    sl::Constants constants{};
    constants.cameraViewToClip = toSLRowMajor(mViewProj.proj);
    constants.clipToCameraView = toSLRowMajor(clipToCameraView);
    constants.clipToPrevClip = toSLRowMajor(clipToPrevClip);
    constants.prevClipToClip = toSLRowMajor(prevClipToClip);
    constants.jitterOffset = sl::float2(0.0f, 0.0f);
    constants.mvecScale = sl::float2(1.0f, 1.0f);
    constants.cameraPinholeOffset = sl::float2(0.0f, 0.0f);
    constants.cameraPos = sl::float3(mCamera.cameraPosition().x, mCamera.cameraPosition().y, mCamera.cameraPosition().z);
    // NO MINUS. The top of the world is the top of the camera.
    constants.cameraUp = sl::float3(invView[1][0], invView[1][1], invView[1][2]);
    // NO MINUS. The right of the world is the right of the camera.
    constants.cameraRight = sl::float3(invView[0][0], invView[0][1], invView[0][2]);
    //With MINUS. Translates the right-hand vector "back to the eyes" into a left-hand vector "forward to the scene"
    constants.cameraFwd = sl::float3(-invView[2][0], -invView[2][1], -invView[2][2]);

    const auto& perspective = mCamera.getPerspective();
    constants.cameraNear = perspective.near_;
    constants.cameraFar = perspective.far_;
    constants.cameraFOV = glm::radians(perspective.fovy);
    constants.cameraAspectRatio = perspective.aspect;

    constants.depthInverted = sl::Boolean::eFalse;
    constants.cameraMotionIncluded = sl::Boolean::eTrue;
    constants.motionVectors3D = sl::Boolean::eFalse;
    constants.reset = (m_slFrameIndex == 0u) ? sl::Boolean::eTrue : sl::Boolean::eFalse;
    constants.motionVectorsDilated = sl::Boolean::eFalse;
    constants.motionVectorsJittered = sl::Boolean::eFalse;

    static const sl::ViewportHandle viewport(0);
    sl::Result constantsRes = _core.slSetConstantsSafe(constants, frameToken, viewport);
    if (constantsRes != sl::Result::eOk && !m_slConstantsErrorLogged) {
        Utils::printLog(INFO_PARAM, "slSetConstants failed, sl::Result=%d", static_cast<int>(constantsRes));
        m_slConstantsErrorLogged = true;
    }
}

void VulkanRenderer::evaluateDLSSPass(uint32_t currentImage, const sl::FrameToken& frameToken) {
    if (!_core.isDlssSupported()) {
        return;
    }

    static const sl::ViewportHandle viewport(0);
    const sl::BaseStructure* inputs[] = {&viewport};

    sl::Result evalRes = _core.slEvaluateFeatureSafe(sl::kFeatureDLSS, frameToken, inputs, static_cast<uint32_t>(std::size(inputs)),
                                                     reinterpret_cast<sl::CommandBuffer*>(_cmdBufs[currentImage]));
    if (evalRes != sl::Result::eOk && !m_slConstantsErrorLogged) {
        Utils::printLog(INFO_PARAM, "slEvaluateFeature failed, sl::Result=%d", static_cast<int>(evalRes));
        m_slConstantsErrorLogged = true;
    }

    Utils::VulkanImageMemoryBarrier(_cmdBufs[currentImage], _dlssOutputBuffer.colorBufferImage[currentImage],
                                _dlssOutputBuffer.colorFormat, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                VK_IMAGE_ASPECT_COLOR_BIT, 1U, 1U, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT,
                                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
    
    const VkImageLayout swapchainOldLayout =
        m_swapchainImageNeedsGeneralTransition[currentImage] ? VK_IMAGE_LAYOUT_PRESENT_SRC_KHR : VK_IMAGE_LAYOUT_UNDEFINED;
    Utils::VulkanImageMemoryBarrier(_cmdBufs[currentImage], _swapChain.images[currentImage], _core.getSurfaceFormat().format,
                                    swapchainOldLayout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT, 1U,
                                    1U, 0, VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                                    VK_PIPELINE_STAGE_TRANSFER_BIT);
    
    VkImageBlit blit{};
    blit.srcOffsets[0] = {0, 0, 0};
    blit.srcOffsets[1] = {_windowWidth, _windowHeight, 1};
    blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    blit.srcSubresource.mipLevel = 0;
    blit.srcSubresource.baseArrayLayer = 0;
    blit.srcSubresource.layerCount = 1;
    blit.dstOffsets[0] = {0, 0, 0};
    blit.dstOffsets[1] = {_windowWidth, _windowHeight, 1};
    blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    blit.dstSubresource.mipLevel = 0;
    blit.dstSubresource.baseArrayLayer = 0;
    blit.dstSubresource.layerCount = 1;

    vkCmdBlitImage(_cmdBufs[currentImage], _dlssOutputBuffer.colorBufferImage[currentImage], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   _swapChain.images[currentImage], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);

    Utils::VulkanImageMemoryBarrier(_cmdBufs[currentImage], _swapChain.images[currentImage], _core.getSurfaceFormat().format,
                                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                                    VK_IMAGE_ASPECT_COLOR_BIT, 1U, 1U, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_MEMORY_READ_BIT,
                                    VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
    m_swapchainImageNeedsGeneralTransition[currentImage] = true;

    Utils::VulkanImageMemoryBarrier(_cmdBufs[currentImage], _dlssOutputBuffer.colorBufferImage[currentImage],
                                    _dlssOutputBuffer.colorFormat, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                    VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_ASPECT_COLOR_BIT, 1U, 1U, VK_ACCESS_TRANSFER_READ_BIT,
                                    VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                                    VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
}
#endif

void VulkanRenderer::init() {
    _core.init();
    // Get properties of our new device
    vkGetPhysicalDeviceProperties(_core.getPhysDevice(), &mDeviceProperties);

    _queue = _core.getAllQueues().at(VulkanCore::Queue_family::GFX_QUEUE_FAMILY).queue;
    if (!_queue) {
        Utils::printLog(ERROR_PARAM, "failed to get graphics queue!");
        return;
    }

#if defined(USE_DLSS) && USE_DLSS
    if (!_core.isDlssSupported() && m_isDlssEnabled) {
        Utils::printLog(INFO_PARAM, "DLSS feature is not loaded; SL resource tags are skipped");
    } else {
        sl::DLSSOptions options{};
        options.mode = sl::DLSSMode::eMaxQuality;
        options.outputWidth = _windowWidth;
        options.outputHeight = _windowHeight;
        options.colorBuffersHDR = sl::Boolean::eFalse;
        options.useAutoExposure = sl::Boolean::eFalse;
        static const sl::ViewportHandle viewport(0);
        sl::Result optionsRes = _core.slDLSSSetOptionsSafe(viewport, options);
        if (optionsRes != sl::Result::eOk) {
            Utils::printLog(INFO_PARAM, "slDLSSSetOptions failed, sl::Result=%d", static_cast<int>(optionsRes));
            m_isDlssEnabled = false;
        }
    }
#endif

    auto swapchainCreateInfo = createSwapChain();
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
    createDescriptorPoolForImGui();

    createFSRContext(swapchainCreateInfo);
}