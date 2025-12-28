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

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE  /// coerce the perspective projection matrix to be in depth: [0.0 to 1.0]
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/transform.hpp>

#include <imgui/backends/imgui_impl_vulkan.h>
#include <imgui/imgui.h>

#if defined(USE_FSR) && USE_FSR
#include <ffx_api/vk/ffx_api_vk.hpp>
#include <FidelityFX/host/backends/vk/ffx_vk.h>
#include <ffx_api/ffx_framegeneration.hpp>
#include <ffx_api/ffx_upscale.hpp>
#endif

#include <btBulletDynamicsCommon.h>

static constexpr float Z_NEAR = 0.1f;
static constexpr float Z_FAR = 1000.0f;
static constexpr float FOV = 65.0f;

// light source position offset from the camera
const static glm::vec3 _lightPos = glm::vec3(0.0f, 0.6f * Z_FAR, -Z_FAR);
// clear depth buffer only once and then we accumulate trails of the vehicle
static bool _oneOffClearingFootPrint = true;
// lastFootPrintPos allows us to draw original print of wheels without noisy messy effect caused by constant redrawing with
// footprint texture overlapping
static glm::vec3 _lastFootPrintPos = glm::vec3(0.0f, -1000.0f, 0.0f);
// if the traveled distance exceeds 70 percentage of panzer lenght then we draw new footprint
float _footPrintRedrawingK = 0.7f;

VulkanRenderer::VulkanRenderer(std::string_view appName, size_t width, size_t height)
    : VulkanState(appName, width, height),
      mTextureFactory(new TextureFactory(*this)),  /// this is not used imedially it's safe
      mCamera({FOV, static_cast<float>(width) / height, Z_NEAR, Z_FAR}, {0.0f, 55.0f, -130.0f}) {
    assert(mTextureFactory);
    using namespace std::literals;

    _pushConstant.windowSize = glm::vec4(_width, _height, Z_FAR, Z_NEAR);
    _pushConstant.lightPos = _lightPos;

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
    m_pipelineCreators[SHADOWMAP].reset(
        new PipelineCreatorShadowMap(this->_shadowMapBuffer, *this, m_renderPassShadowMap, "vert_shadowMap.spv", "frag_shadowMap.spv"));
    m_pipelineCreators[POST_LIGHTING].reset(new PipelineCreatorQuad(
        *this, m_renderPass, "vert_gLigtingSubpass.spv", "frag_gLigtingSubpass.spv", true, true, 2u, m_pushConstantRange));
    m_pipelineCreators[POST_FXAA].reset(new PipelineCreatorQuad(*this, m_renderPassFXAA, "vert_fxaa.spv", "frag_fxaa.spv",
                                                                &this->_colorBuffer, false, false, m_pushConstantRange));
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
                                                                 "vert_depthWriter.spv", "frag_depthWriter.spv"));
    m_pipelineCreators[SSAO].reset(
        new PipelineCreatorSSAO(*this, m_renderPass, "vert_ssao.spv", "frag_ssao.spv", 1u, m_pushConstantRange));
    m_pipelineCreators[FOOTPRINT].reset(
        new PipelineCreatorFootprint(this->_footprintBuffer, *this, m_renderPassFootprint, "vert_footPrint.spv", "frag_footPrint.spv"));
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
        std::uniform_real<> distrScale(0.5, 1.0);
        int32_t gridLen = std::floor(std::sqrt(semiTransparentInstances.size()));
        float step = 2.0f * limit / gridLen;
        //std::uniform_real<> distr(0.0f, 0.1f * step);
        const float startX = -limit;
        const float startZ = -limit;
        for (std::size_t i = 0u; i < semiTransparentInstances.size(); ++i) {
            auto& instance = semiTransparentInstances[i];
            //float xOffset = distr(gen);
            //float zOffset = distr(gen);
            instance.posShift.y = 0.0f;

            instance.scale = distrScale(gen);

            auto row = i / gridLen;
            auto col = i % gridLen;
            instance.posShift.x = startX + row * step;
            instance.posShift.z = startZ + col * step;
        }

        m_semiTransparentModels.emplace_back(
            new ObjModel(*this, *mTextureFactory, "highpoly_tree_trunk.obj"sv,
                         static_cast<PipelineCreatorTextured*>(m_pipelineCreators[SEMI_TRANSPARENT].get()), nullptr, 
                         60.0f, semiTransparentInstances));
        m_semiTransparentModels.emplace_back(
            new MD5Model("tree_leaves.md5mesh"sv, "tree_leaves_idle.md5anim"sv, *this, *mTextureFactory,
                         static_cast<PipelineCreatorTextured*>(m_pipelineCreators[SEMI_TRANSPARENT].get()), nullptr, 
                         10.0f, 0.1f, true, semiTransparentInstances));
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
        btCollisionShape* boxShape = new btBoxShape(btVector3(1, 60 / 2.0f, 1)); // Bullet uses half-extents (size 60 / 2.0f means width 60)
        btScalar mass = 1.0f;
        btVector3 localInertia(0, 0, 0);
        boxShape->calculateLocalInertia(mass, localInertia);

        btTransform startTransform;
        startTransform.setIdentity();
        startTransform.setOrigin(btVector3(0, 0, 0)); // TODO
        btDefaultMotionState* motionState = new btDefaultMotionState(startTransform);
        btRigidBody::btRigidBodyConstructionInfo treeRBInfo(mass, motionState, boxShape, localInertia);
        btRigidBody* treeBody = new btRigidBody(treeRBInfo);
        dynamicsWorld->addRigidBody(treeBody);
    }

    //// 5. Run Simulation
    //for (int i = 0; i < 150; i++) {
    //    dynamicsWorld->stepSimulation(1.f / 60.f, 10);
    //    btTransform trans;
    //    fallBody->getMotionState()->getWorldTransform(trans);
    //    std::cout << "Cube Height: " << trans.getOrigin().getY() << std::endl;
    //}
}

VulkanRenderer::~VulkanRenderer() {
    Pipeliner::getInstance().saveCache();

    cleanupSwapChain();
#if defined(USE_FSR) && USE_FSR
    if (mFSRSwapChainContext)
        ffxDestroyContext(&mFSRSwapChainContext, nullptr);
#endif

    mTextureFactory.reset(nullptr);

    _aligned_free(mp_modelTransferSpace);

    m_semiTransparentModels.clear();

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

        vkDestroyImageView(_core.getDevice(), _shadingBuffer.colorBufferImageView[i], nullptr);
        vkDestroyImage(_core.getDevice(), _shadingBuffer.colorBufferImage[i], nullptr);
        vkFreeMemory(_core.getDevice(), _shadingBuffer.colorBufferImageMemory[i], nullptr);

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
    if (_width != width || _height != height) {
        cleanupSwapChain();

        _width = width;
        _height = height;
        m_currentFrame = 0u;
        _oneOffClearingFootPrint = true;
        _lastFootPrintPos = glm::vec3(0.0f, -1000.0f, 0.0f);

        _pushConstant.windowSize = glm::vec4(_width, _height, Z_FAR, Z_NEAR);
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

        createFSRContext(swapchainCreateInfo);
    }
}

// FSR 3 frame generation
void VulkanRenderer::createFSRContext(VkSwapchainCreateInfoKHR swapchainCreateInfo) {
#if defined(USE_FSR) && USE_FSR
    if (mFSRSwapChainContext) {
        return;
    }

    ffx::CreateContextDescFrameGenerationSwapChainVK createSwapChainDesc{};
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

    ffx::ReturnCode retCode = ffx::CreateContext(mFSRSwapChainContext, nullptr, createSwapChainDesc);
    if (retCode != ffx::ReturnCode::Ok) {
        Utils::printLog(ERROR_PARAM, "Failed to create FSR SwapChain context", static_cast<uint32_t>(retCode));
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

    ffx::CreateBackendVKDesc backendDesc{};
    backendDesc.vkDevice = _core.getDevice();
    backendDesc.vkPhysicalDevice = _core.getPhysDevice();
    backendDesc.vkDeviceProcAddr = vkGetDeviceProcAddr;

    ffx::CreateContextDescFrameGeneration createFg{};
    createFg.displaySize = {_width, _height};
    createFg.maxRenderSize = {_width, _height};
    createFg.flags = FFX_FRAMEGENERATION_ENABLE_DEBUG_CHECKING;
    createFg.flags |= FFX_FRAMEGENERATION_ENABLE_ASYNC_WORKLOAD_SUPPORT;

    createFg.backBufferFormat = ffxApiGetSurfaceFormatVK(_core.getSurfaceFormat().format);

    //retCode = ffx::CreateContext(mFSRFrameGenContext, nullptr, createFg, backendDesc);
    if (retCode != ffx::ReturnCode::Ok) {
        Utils::printLog(ERROR_PARAM, "Failed to create FSR FG context: ", static_cast<uint32_t>(retCode));
    }
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
        glm::vec4(-4.0f, 19.0f, -30.0f, 1.0f);  // Note: here we use hardcorded position of pipe in our model!!!
    m_particles[3]->update(currentImage, deltaMS, exhaustPipePos1, velocity);
    glm::vec4 exhaustPipePos2 =
        mCamera.targetModelMat() *
        glm::vec4(4.0f, 19.0f, -30.0f, 1.0f);  // Note: here we use hardcorded position of pipe in our model!!!
    m_particles[4]->update(currentImage, deltaMS, exhaustPipePos2, velocity);

    const auto objectsAmount = m_models.size();

    const auto& cameraViewProj = mCamera.viewProjMat();
    const auto& model = mCamera.targetModelMat();

    mViewProj.viewProj = cameraViewProj.proj * cameraViewProj.view;
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
        pModel->model = identityMatrix;
        pModel->MVP = mViewProj.viewProj;
    }
    // set target model matrix from Camera for our main 3d model
    Model* pModel = (Model*)((uint64_t)mp_modelTransferSpace);
    pModel->model = model;
    pModel->MVP = mViewProj.viewProj * pModel->model;

    // rotate skybox slowly
    pModel = (Model*)((uint64_t)mp_modelTransferSpace + (objectsAmount - 1) * _modelUniformAlignment);
    static float skyboxRotationDegree = 0.0f;
    skyboxRotationDegree += 0.0001f * kDelay;
    glm::mat4 rotMat = glm::rotate(glm::radians(static_cast<float>(skyboxRotationDegree)), glm::vec3(0.0f, 1.0f, 0.0f));
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
            treeTrunkInstance.model_col0 = glm::packHalf4x16(modelMat[0]);
            treeTrunkInstance.model_col1 = glm::packHalf4x16(modelMat[1]);
            treeTrunkInstance.model_col2 = glm::packHalf4x16(modelMat[2]);
            treeTrunkInstance.model_col3 = glm::packHalf4x16(modelMat[3]);

            treeCrownInstances[i].model_col0 = treeTrunkInstance.model_col0;
            treeCrownInstances[i].model_col1 = treeTrunkInstance.model_col1;
            treeCrownInstances[i].model_col2 = treeTrunkInstance.model_col2;
            treeCrownInstances[i].model_col3 = treeTrunkInstance.model_col3;
            continue;
        }
        
        auto tarpos = mCamera.targetPos();
        auto dist = glm::distance(treeTrunkInstance.posShift, mCamera.targetPos());
        auto boundingRadiuses = 0.5f * m_models[0]->radius(); //we can skip it for trees '+m_semiTransparentModels[0]->radius() * instance.scale;'
        if (boundingRadiuses >= dist) {
            m_semiTransparentAnimations[i].startAnimation(2000.0f);  // 2 seconds
        }
    }

    for (size_t i = objectsAmount; i < (objectsAmount + m_semiTransparentModels.size()); i++) {
        Model* pModel = (Model*)((uint64_t)mp_modelTransferSpace + (i * _modelUniformAlignment));
        pModel->model = m_semiTransparentAnimations[0].getModelMat();
        pModel->MVP = mViewProj.viewProj;
    }

    if (glm::distance(_pushConstant.lightPos, glm::vec3(_pushConstant.lightPos.x, _pushConstant.lightPos.y, mCamera.targetPos().z)) >
        0.5f * Z_FAR) {
        _pushConstant.lightPos = glm::vec3(_pushConstant.lightPos.x, _pushConstant.lightPos.y, mCamera.targetPos().z);
    }
    m_lightViewProj =
        glm::ortho(-Z_FAR, Z_FAR, -Z_FAR, Z_FAR, -Z_FAR, Z_FAR) *
        glm::lookAt(_pushConstant.lightPos, glm::vec3(-_lightPos.x, 0.0f, -_lightPos.z), glm::vec3(0.0f, 1.0f, 0.0f));
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

void VulkanRenderer::recordCommandBuffers(uint32_t currentImage, bool hmiRenderData) {
    static VkCommandBufferBeginInfo beginInfo {
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        nullptr,
        VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT,
        nullptr
    };

    VkResult res = vkBeginCommandBuffer(_cmdBufs[currentImage], &beginInfo);
    CHECK_VULKAN_ERROR("vkBeginCommandBuffer error %d\n", res);
    
    const static VkClearValue zeroClearValues{{0.0f, 0.0f, 0.0f, 0.0f}};

    //---------------------------------------------------------------------------------------------//
    /// depth writing pass
    static std::array<VkClearValue, 2> depthWriterClearValues{zeroClearValues, zeroClearValues};
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
        m_semiTransparentModels[meshIndex]->drawWithCustomPipeline(m_pipelineCreators[SHADOWMAP].get(), _cmdBufs[currentImage], currentImage,
                                                            dynamicOffset);
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

    Utils::VulkanImageMemoryBarrier(_cmdBufs[currentImage], _footprintBuffer.depthImage, _footprintBuffer.depthFormat,
                                    VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL, VK_IMAGE_ASPECT_DEPTH_BIT, 1U,
                                    1U, VK_ACCESS_NONE, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                                    VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT);

    vkCmdBeginRenderPass(_cmdBufs[currentImage], &renderPassFootprintInfo, VK_SUBPASS_CONTENTS_INLINE);

    if (_oneOffClearingFootPrint) {
        VkClearValue footPrintClearValues{};
        footPrintClearValues.depthStencil.depth = 1.0f;
        VkClearAttachment clearAttachment;
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
    VkClearValue clearValue;
    clearValue.color = {0.0f, 0.0f, 0.0f, 1.0f};
    std::vector<VkClearValue> clearValues(10, clearValue);
    clearValues[7] = VkClearValue{};
    clearValues[7].depthStencil.depth = 1.0f;

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

    ///-----------------------------------------------------------------------------------///
    /// Start second subpass (SSAO)
    vkCmdNextSubpass(_cmdBufs[currentImage], VK_SUBPASS_CONTENTS_INLINE);

    /// quad subpass
    {
        const auto& pipelineCreator = m_pipelineCreators[SSAO];
        vkCmdPushConstants(_cmdBufs[currentImage], pipelineCreator->getPipeline()->pipelineLayout, PUSH_CONSTANT_STAGE_FLAGS,
                           0, sizeof(PushConstant), &_pushConstant);
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
    renderPassSSAOblurInfo.renderArea.offset.x = 0;
    renderPassSSAOblurInfo.renderArea.offset.y = 0;
    renderPassSSAOblurInfo.renderArea.extent.width = _width;
    renderPassSSAOblurInfo.renderArea.extent.height = _height;
    renderPassSSAOblurInfo.clearValueCount = ssaoBlurClearValues.size();
    renderPassSSAOblurInfo.pClearValues = ssaoBlurClearValues.data();
    renderPassSSAOblurInfo.framebuffer = m_fbsSSAOblur[currentImage];

    Utils::VulkanImageMemoryBarrier(_cmdBufs[currentImage], _colorBuffer.colorBufferImage[currentImage], _colorBuffer.colorFormat,
                                    VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                    VK_IMAGE_ASPECT_COLOR_BIT, 1U, 1U, VK_ACCESS_NONE, VK_ACCESS_COLOR_ATTACHMENT_READ_BIT,
                                    VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);

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
        renderPassGaussXBloomInfo.renderArea.offset.x = 0;
        renderPassGaussXBloomInfo.renderArea.offset.y = 0;
        renderPassGaussXBloomInfo.renderArea.extent.width = _width;
        renderPassGaussXBloomInfo.renderArea.extent.height = _height;
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
        renderPassGaussYBloomInfo.renderArea.offset.x = 0;
        renderPassGaussYBloomInfo.renderArea.offset.y = 0;
        renderPassGaussYBloomInfo.renderArea.extent.width = _width;
        renderPassGaussYBloomInfo.renderArea.extent.height = _height;
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
    renderPassBloomInfo.renderArea.offset.x = 0;
    renderPassBloomInfo.renderArea.offset.y = 0;
    renderPassBloomInfo.renderArea.extent.width = _width;
    renderPassBloomInfo.renderArea.extent.height = _height;
    renderPassBloomInfo.clearValueCount = bloomClearValues.size();
    renderPassBloomInfo.pClearValues = bloomClearValues.data();
    renderPassBloomInfo.framebuffer = m_fbsBloom[currentImage];

    Utils::VulkanImageMemoryBarrier(_cmdBufs[currentImage], _colorBuffer.colorBufferImage[currentImage], _colorBuffer.colorFormat,
                                    VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                    VK_IMAGE_ASPECT_COLOR_BIT, 1U, 1U, VK_ACCESS_NONE, VK_ACCESS_COLOR_ATTACHMENT_READ_BIT,
                                    VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);

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
    renderPassSemiTransInfo.renderArea.offset.x = 0;
    renderPassSemiTransInfo.renderArea.offset.y = 0;
    renderPassSemiTransInfo.renderArea.extent.width = _width;
    renderPassSemiTransInfo.renderArea.extent.height = _height;
    renderPassSemiTransInfo.clearValueCount = semiTransClearValues.size();
    renderPassSemiTransInfo.pClearValues = semiTransClearValues.data();
    renderPassSemiTransInfo.framebuffer = m_fbsSemiTrans[currentImage];

    Utils::VulkanImageMemoryBarrier(_cmdBufs[currentImage], _colorBuffer.colorBufferImage[currentImage], _colorBuffer.colorFormat,
                                    VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                    VK_IMAGE_ASPECT_COLOR_BIT, 1U, 1U, VK_ACCESS_NONE, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                                    VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                                    VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);

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
    
    //---------------------------------------------------------------------------------------------//
    /// FXAA render pass

    static std::array<VkClearValue, 2> fxaaClearValues;
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
                                    VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                    VK_IMAGE_ASPECT_COLOR_BIT, 1U, 1U, VK_ACCESS_NONE,
                                    VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                                    VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);

    vkCmdBeginRenderPass(_cmdBufs[currentImage], &renderPassFXAAInfo, VK_SUBPASS_CONTENTS_INLINE);
    {
        const auto& pipelineCreator = m_pipelineCreators[POST_FXAA];
        vkCmdPushConstants(_cmdBufs[currentImage], pipelineCreator->getPipeline()->pipelineLayout, PUSH_CONSTANT_STAGE_FLAGS, 0,
                           sizeof(PushConstant), &_pushConstant);
        vkCmdBindPipeline(_cmdBufs[currentImage], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineCreator->getPipeline()->pipeline);
        vkCmdBindDescriptorSets(_cmdBufs[currentImage], VK_PIPELINE_BIND_POINT_GRAPHICS,
                                pipelineCreator->getPipeline()->pipelineLayout, 0, 1,
                                pipelineCreator->getDescriptorSet(currentImage), 0, nullptr);
    }

    vkCmdDraw(_cmdBufs[currentImage], 6, 1, 0, 0);

    if (hmiRenderData) {
        _core.getWinController()->imGuiNewFrame(_cmdBufs[currentImage]);
    }

    vkCmdEndRenderPass(_cmdBufs[currentImage]);

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

        // HDR render targets for Bloom effect
        for (auto& buf : _bloomBuffer) {
            buf.colorFormat = HDRFormat;
            Utils::VulkanCreateImage(_core.getDevice(), _core.getPhysDevice(), _width, _height, buf.colorFormat,
                                     VK_IMAGE_TILING_OPTIMAL,
                                     VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT,
                                     VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, buf.colorBufferImage[i], buf.colorBufferImageMemory[i]);

            Utils::VulkanCreateImageView(_core.getDevice(), buf.colorBufferImage[i], buf.colorFormat, VK_IMAGE_ASPECT_COLOR_BIT,
                                         buf.colorBufferImageView[i]);
        }

        // SSAO render target
        Utils::VulkanCreateImage(
            _core.getDevice(), _core.getPhysDevice(), _width, _height, _ssaoBuffer.colorFormat, VK_IMAGE_TILING_OPTIMAL,
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, _ssaoBuffer.colorBufferImage[i], _ssaoBuffer.colorBufferImageMemory[i]);

        Utils::VulkanCreateImageView(_core.getDevice(), _ssaoBuffer.colorBufferImage[i], _ssaoBuffer.colorFormat,
                                     VK_IMAGE_ASPECT_COLOR_BIT, _ssaoBuffer.colorBufferImageView[i]);

        // view space position render target
        Utils::VulkanCreateImage(
            _core.getDevice(), _core.getPhysDevice(), _width, _height, _viewSpaceBuffer.colorFormat, VK_IMAGE_TILING_OPTIMAL,
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            _viewSpaceBuffer.colorBufferImage[i],
            _viewSpaceBuffer.colorBufferImageMemory[i]);

        Utils::VulkanCreateImageView(_core.getDevice(), _viewSpaceBuffer.colorBufferImage[i], _viewSpaceBuffer.colorFormat,
                                     VK_IMAGE_ASPECT_COLOR_BIT, _viewSpaceBuffer.colorBufferImageView[i]);

        // SHADING render target
        Utils::VulkanCreateImage(
            _core.getDevice(), _core.getPhysDevice(), _width, _height, _shadingBuffer.colorFormat, VK_IMAGE_TILING_OPTIMAL,
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            _shadingBuffer.colorBufferImage[i], _shadingBuffer.colorBufferImageMemory[i]);

        Utils::VulkanCreateImageView(_core.getDevice(), _shadingBuffer.colorBufferImage[i], _shadingBuffer.colorFormat,
                                     VK_IMAGE_ASPECT_COLOR_BIT, _shadingBuffer.colorBufferImageView[i]);
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
    _pushConstant.windDirElapsedTimeMS.w += deltaTime;

    // -- GET NEXT IMAGE --
    // Wait for given fence to signal (open) from last draw before continuing
    vkWaitForFences(_core.getDevice(), 1, &m_drawFences[m_currentFrame], VK_TRUE, UINT64_MAX);
    // Manually reset (close) fences
    vkResetFences(_core.getDevice(), 1, &m_drawFences[m_currentFrame]);

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

    // Note: it's just fall back sover for emergency situation
    {
        static uint32_t last_ImageIndex = -1;
        if (last_ImageIndex == ImageIndex) {
            ImageIndex = ++ImageIndex % MAX_FRAMES_IN_FLIGHT;
            Utils::printLog(INFO_PARAM, "vkAcquireNextImageKHR returned the same ImageIndex", last_ImageIndex,
                            " as last one, trying to acquire next image manually !");
        }
        last_ImageIndex = ImageIndex;
    }

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

    updateUniformBuffer(ImageIndex, deltaTime);
    static bool isGPUCalculationFavorable = false;
    if (windowQueueMSG.hmiStates) {
        isGPUCalculationFavorable = windowQueueMSG.hmiStates->gpuAnimationEnabled.second;
    }

    for (auto& model : m_models) {
        model->update(deltaTime, 0, isGPUCalculationFavorable, ImageIndex, mViewProj.viewProj, Z_FAR);
    }

    for (auto& model : m_semiTransparentModels) {
        model->update(deltaTime, 0, isGPUCalculationFavorable, ImageIndex, mViewProj.viewProj, Z_FAR);
    }

    recordCommandBuffers(ImageIndex, windowQueueMSG.hmiRenderData);

    res = vkQueueSubmit(_queue, 1, &submitInfo, m_drawFences[m_currentFrame]);
    CHECK_VULKAN_ERROR("vkQueueSubmit error %d\n", res);

    VkPresentInfoKHR presentInfo = {};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &_swapChain.handle;
    presentInfo.pImageIndices = &ImageIndex;
    presentInfo.pWaitSemaphores = &m_renderCompleteSem[m_currentFrame];
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
        recreateSwapChain(_width, _height);
    } else {
        CHECK_VULKAN_ERROR("vkQueuePresentKHR error %d\n", res);
    }

    // Get next frame (use % MAX_FRAME_DRAWS to keep value below MAX_FRAME_DRAWS)
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

    VkAttachmentDescription hdrBloomAttachment = colorAttachment;
    hdrBloomAttachment.format = _bloomBuffer[0].colorFormat;

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

    VkAttachmentDescription depthSSAOReadyAttachment =
        depthAttachment;  // already initialized depth texture from early renderPass
    depthSSAOReadyAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    depthSSAOReadyAttachment.initialLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    depthSSAOReadyAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL;

    VkAttachmentDescription shadowMapLoadAttachment = depthSSAOReadyAttachment;

    VkAttachmentDescription footPrintLoadAttachment = depthSSAOReadyAttachment;

    VkAttachmentDescription viewSpacePosAttachment = colorAttachment;
    viewSpacePosAttachment.format = _viewSpaceBuffer.colorFormat;
    viewSpacePosAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    viewSpacePosAttachment.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference colorAttachmentRef{};
    colorAttachmentRef.attachment = 0;
    colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference gPassNormalAttachmentRef = colorAttachmentRef;
    gPassNormalAttachmentRef.attachment = 1;

    VkAttachmentReference gPassColorAttachmentRef = colorAttachmentRef;
    gPassColorAttachmentRef.attachment = 2;

    VkAttachmentReference depthAttachmentRef{};
    depthAttachmentRef.attachment = 7;  // temporary depth buffer needed only for correct geometry output in g-pass
    depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;

    VkAttachmentReference footPrintAttachmentRef{};
    footPrintAttachmentRef.attachment = 8;  // depthbuf with trails
    footPrintAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL;

    std::array<VkAttachmentReference, 3> gPassAttachment{colorAttachmentRef, gPassNormalAttachmentRef, gPassColorAttachmentRef};

    subpasses[0].pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpasses[0].colorAttachmentCount = gPassAttachment.size();
    subpasses[0].pColorAttachments = &gPassAttachment[0];
    subpasses[0].pDepthStencilAttachment = &depthAttachmentRef;
    subpasses[0].inputAttachmentCount = 1u;
    subpasses[0].pInputAttachments = &footPrintAttachmentRef;

    // Set up Subpass 2 (SSAO)

    // References to attachments that subpass will take input from
    std::array<VkAttachmentReference, 3> inputSSAOReferences;
    inputSSAOReferences[0].attachment = 1;
    inputSSAOReferences[0].layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    inputSSAOReferences[1].attachment = 4;
    inputSSAOReferences[1].layout = VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL;
    inputSSAOReferences[2].attachment = 10;
    inputSSAOReferences[2].layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkAttachmentReference colorAttachmentSSAOReference = {};
    colorAttachmentSSAOReference.attachment = 3;
    colorAttachmentSSAOReference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    subpasses[1].pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpasses[1].colorAttachmentCount = 1;
    subpasses[1].pColorAttachments = &colorAttachmentSSAOReference;
    subpasses[1].inputAttachmentCount = static_cast<uint32_t>(inputSSAOReferences.size());
    subpasses[1].pInputAttachments = inputSSAOReferences.data();

    // Set up Subpass 3 (lighting)

    // References to attachments that subpass will take input from
    std::array<VkAttachmentReference, 5> inputReferences;
    inputReferences[0].attachment = 1;
    inputReferences[0].layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    inputReferences[1].attachment = 2;
    inputReferences[1].layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    inputReferences[2].attachment = 3;
    inputReferences[2].layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;  // SSAO
    inputReferences[3].attachment = 4;
    inputReferences[3].layout = VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL;
    inputReferences[4].attachment = 5;
    inputReferences[4].layout = VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL;

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
    std::array<VkSubpassDependency, 5> subpassDependencies;

    // for color buffer from VK_IMAGE_LAYOUT_UNDEFINED to VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
    subpassDependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    subpassDependencies[0].dstSubpass = 0;
    subpassDependencies[0].srcStageMask = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;  // for external depth buffer using
    subpassDependencies[0].srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
    subpassDependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                                          VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    subpassDependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                                           VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                                           VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    // Subpass 1 layout (color/depth) to Subpass 2 layout (shader read)
    subpassDependencies[1].srcSubpass = 0;
    subpassDependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    subpassDependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                                           VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                                           VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    subpassDependencies[1].dstSubpass = 1;
    subpassDependencies[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    subpassDependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    subpassDependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

    // Subpass 2 layout (color/depth) to Subpass 3 layout (shader read)
    subpassDependencies[2].srcSubpass = 1;
    subpassDependencies[2].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    subpassDependencies[2].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                                           VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
                                           VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
    subpassDependencies[2].dstSubpass = 2;
    subpassDependencies[2].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    subpassDependencies[2].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    subpassDependencies[2].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

    subpassDependencies[3].srcSubpass = 0;
    subpassDependencies[3].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    subpassDependencies[3].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                                           VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                                           VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
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

    std::array<VkAttachmentDescription, 11> renderPassAttachments = {
        colorAttachment,          gPassNormalAttachment,   gPassColorAttachment, colorAttachmentSSAO,
        depthSSAOReadyAttachment, shadowMapLoadAttachment, hdrBloomAttachment,   depthTemporaryAttachment,
        footPrintLoadAttachment,  colorAttachmentShading,  viewSpacePosAttachment};

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
    colorAttachmentSemiTrans.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkAttachmentDescription depthAttachmentSemiTrans = depthAttachment;
    depthAttachmentSemiTrans.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    depthAttachmentSemiTrans.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthAttachmentSemiTrans.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachmentSemiTrans.initialLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    depthAttachmentSemiTrans.finalLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;

    VkAttachmentReference colorAttachmentSemiTransReference = {};
    colorAttachmentSemiTransReference.attachment = 0;
    colorAttachmentSemiTransReference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depthAttachmentSemiTransReference;
    depthAttachmentSemiTransReference.attachment = 1;
    depthAttachmentSemiTransReference.layout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpassSemiTrans{};
    subpassSemiTrans.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpassSemiTrans.colorAttachmentCount = 1;
    subpassSemiTrans.pColorAttachments = &colorAttachmentSemiTransReference;
    subpassSemiTrans.inputAttachmentCount = 0;
    subpassSemiTrans.pDepthStencilAttachment = &depthAttachmentSemiTransReference;

    std::array<VkAttachmentDescription, 2> renderPassAttachmentsSemiTrans = {colorAttachmentSemiTrans, depthAttachmentSemiTrans};

    std::array<VkSubpassDependency, 2u> dependencySemiTrans;
    // color dependancy
    dependencySemiTrans[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    dependencySemiTrans[0].dstSubpass = 0;
    dependencySemiTrans[0].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependencySemiTrans[0].srcAccessMask = 0;
    dependencySemiTrans[0].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dependencySemiTrans[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    // depth dependency (depth attachment cannot be used before previous renderpasses have finished using it)
    dependencySemiTrans[1].srcSubpass = VK_SUBPASS_EXTERNAL;
    dependencySemiTrans[1].dstSubpass = 0;
    dependencySemiTrans[1].srcStageMask = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    dependencySemiTrans[1].srcAccessMask = 0;
    dependencySemiTrans[1].dstStageMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
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
    colorAttachment1BlurX.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    VkAttachmentDescription colorAttachment2BlurX = colorAttachment1BlurX;
    colorAttachment2BlurX.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    colorAttachment2BlurX.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttachment2BlurX.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkAttachmentReference bloomHdrAttachmentReferenceBlurX = {};
    bloomHdrAttachmentReferenceBlurX.attachment = 0;
    bloomHdrAttachmentReferenceBlurX.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    // References to attachments that subpass will take input from
    VkAttachmentReference inputBloomHdrReferenceBlurX;
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
    std::array<VkSubpassDependency, 1u> dependencyBlurX;

    dependencyBlurX[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    dependencyBlurX[0].dstSubpass = 0;
    dependencyBlurX[0].srcStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    dependencyBlurX[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependencyBlurX[0].srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
    dependencyBlurX[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
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
    colorAttachment1BlurY.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttachment1BlurY.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    VkAttachmentDescription colorAttachment2BlurY = colorAttachment1BlurY;
    colorAttachment2BlurY.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    colorAttachment2BlurY.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttachment2BlurY.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkAttachmentReference bloomHdrAttachmentReferenceBlurY = {};
    bloomHdrAttachmentReferenceBlurY.attachment = 0;
    bloomHdrAttachmentReferenceBlurY.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    // References to attachments that subpass will take input from
    VkAttachmentReference inputBloomHdrReferenceBlurY;
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
    std::array<VkSubpassDependency, 1u> dependencyBlurY;

    dependencyBlurY[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    dependencyBlurY[0].dstSubpass = 0;
    dependencyBlurY[0].srcStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    dependencyBlurY[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependencyBlurY[0].srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
    dependencyBlurY[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
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
    colorAttachment1Bloom.format = _core.getSurfaceFormat().format;
    colorAttachment1Bloom.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment1Bloom.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    colorAttachment1Bloom.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment1Bloom.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment1Bloom.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment1Bloom.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttachment1Bloom.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    VkAttachmentDescription colorAttachment2Bloom = colorAttachment1Bloom;
    colorAttachment2Bloom.format = _bloomBuffer[0].colorFormat;
    colorAttachment2Bloom.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttachment2Bloom.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkAttachmentReference bloomHdrAttachmentReferenceBloom = {};
    bloomHdrAttachmentReferenceBloom.attachment = 0;
    bloomHdrAttachmentReferenceBloom.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    // References to attachments that subpass will take input from
    VkAttachmentReference inputBloomHdrReferenceBloom;
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
    std::array<VkSubpassDependency, 1u> dependencyBloom;

    dependencyBloom[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    dependencyBloom[0].dstSubpass = 0;
    dependencyBloom[0].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependencyBloom[0].srcAccessMask = 0;
    dependencyBloom[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependencyBloom[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

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
    VkAttachmentDescription colorAttachmentFXAA = {};
    colorAttachmentFXAA.format = _core.getSurfaceFormat().format;
    colorAttachmentFXAA.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachmentFXAA.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    colorAttachmentFXAA.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachmentFXAA.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachmentFXAA.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;

    colorAttachmentFXAA.initialLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    colorAttachmentFXAA.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

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
    VkSubpassDependency dependency;

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

    VkSubpassDescription subpassesDepthSSAO{};
    subpassesDepthSSAO.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpassesDepthSSAO.colorAttachmentCount = 1;
    subpassesDepthSSAO.pColorAttachments = &viewSpacePosAttachmentForSSAORef;
    subpassesDepthSSAO.pDepthStencilAttachment = &depthAttachmentForSSAORef;

    // Subpass dependencies for layout transitions
    std::array<VkSubpassDependency, 2u> dependencyDepthAndViewSpacePosForSSAO;

    // depth
    dependencyDepthAndViewSpacePosForSSAO[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    dependencyDepthAndViewSpacePosForSSAO[0].dstSubpass = 0;
    dependencyDepthAndViewSpacePosForSSAO[0].srcStageMask = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT; // store previous 'clear' operation
    // layout transition happens here from depth 'clear'
    dependencyDepthAndViewSpacePosForSSAO[0].dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT; 
    dependencyDepthAndViewSpacePosForSSAO[0].srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;  // 'clear' writes to depth buffer
    dependencyDepthAndViewSpacePosForSSAO[0].dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
                                        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;  // also we read 'cleared' depth buffer
    dependencyDepthAndViewSpacePosForSSAO[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

    // viewspace pos color
    dependencyDepthAndViewSpacePosForSSAO[1].srcSubpass = VK_SUBPASS_EXTERNAL;
    dependencyDepthAndViewSpacePosForSSAO[1].dstSubpass = 0;
    dependencyDepthAndViewSpacePosForSSAO[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;  // store previous 'clear' operation
    dependencyDepthAndViewSpacePosForSSAO[1].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependencyDepthAndViewSpacePosForSSAO[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;  // 'clear' writes to color buffer
    dependencyDepthAndViewSpacePosForSSAO[1].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |  VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
    dependencyDepthAndViewSpacePosForSSAO[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

    VkAttachmentDescription colorAttachmentViewSpacePos = colorAttachment;
    std::array<VkAttachmentDescription, 2> renderPassAttachmentsSSAO = {depthAttachment, colorAttachmentViewSpacePos};

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
    depthAttachmentFootPrint.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depthAttachmentFootPrint.initialLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    depthAttachmentFootPrint.finalLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
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
    VkSubpassDependency dependencyFootPrint;

    dependencyFootPrint.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependencyFootPrint.dstSubpass = 0;
    dependencyFootPrint.srcStageMask = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    dependencyFootPrint.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
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
    colorAttachmentSSAOblurOutput.format = _core.getSurfaceFormat().format;
    colorAttachmentSSAOblurOutput.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachmentSSAOblurOutput.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    colorAttachmentSSAOblurOutput.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachmentSSAOblurOutput.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachmentSSAOblurOutput.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachmentSSAOblurOutput.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttachmentSSAOblurOutput.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    VkAttachmentDescription colorAttachmentSSAOblurInput = colorAttachmentSSAOblurOutput;  // SSAO input for blurring
    colorAttachmentSSAOblurInput.format = _shadingBuffer.colorFormat;
    colorAttachmentSSAOblurInput.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttachmentSSAOblurInput.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkAttachmentReference attachmentReferenceSSAOblurOutput = {};
    attachmentReferenceSSAOblurOutput.attachment = 0;
    attachmentReferenceSSAOblurOutput.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference attachmentReferenceSSAOblurInput;
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
    std::array<VkSubpassDependency, 1u> dependencySSAOblur;

    dependencySSAOblur[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    dependencySSAOblur[0].dstSubpass = 0;
    dependencySSAOblur[0].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependencySSAOblur[0].srcAccessMask = 0;
    dependencySSAOblur[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependencySSAOblur[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

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
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        std::array<VkImageView, 11> attachments = {_colorBuffer.colorBufferImageView[i],
                                                   _gPassBuffer.normal.colorBufferImageView[i],
                                                   _gPassBuffer.color.colorBufferImageView[i],
                                                   _ssaoBuffer.colorBufferImageView[i],
                                                   _depthBuffer.depthImageView,
                                                   _shadowMapBuffer.depthImageView,
                                                   _bloomBuffer[0].colorBufferImageView[i],
                                                   _depthTempBuffer.depthImageView,
                                                   _footprintBuffer.depthImageView,
                                                   _shadingBuffer.colorBufferImageView[i],
                                                   _viewSpaceBuffer.colorBufferImageView[i]};

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
    // FBO Gauss x blurring for bloom effect
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        std::array<VkImageView, 2> attachments = {_bloomBuffer[1].colorBufferImageView[i],
                                                  _bloomBuffer[0].colorBufferImageView[i]};

        VkFramebufferCreateInfo fbCreateInfo = {};
        fbCreateInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbCreateInfo.renderPass = m_renderPassXBlur;
        fbCreateInfo.attachmentCount = attachments.size();
        fbCreateInfo.pAttachments = attachments.data();
        fbCreateInfo.width = _width;
        fbCreateInfo.height = _height;
        fbCreateInfo.layers = 1;

        res = vkCreateFramebuffer(_core.getDevice(), &fbCreateInfo, nullptr, &m_fbsXBlur[i]);
        CHECK_VULKAN_ERROR("vkCreateFramebuffer Gauss x blurring error %d\n", res);
    }

    //-------------------------------------------------------//
    // FBO Gauss y blurring for bloom effect
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        std::array<VkImageView, 2> attachments = {_bloomBuffer[0].colorBufferImageView[i],
                                                  _bloomBuffer[1].colorBufferImageView[i]};

        VkFramebufferCreateInfo fbCreateInfo = {};
        fbCreateInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbCreateInfo.renderPass = m_renderPassYBlur;
        fbCreateInfo.attachmentCount = attachments.size();
        fbCreateInfo.pAttachments = attachments.data();
        fbCreateInfo.width = _width;
        fbCreateInfo.height = _height;
        fbCreateInfo.layers = 1;

        res = vkCreateFramebuffer(_core.getDevice(), &fbCreateInfo, nullptr, &m_fbsYBlur[i]);
        CHECK_VULKAN_ERROR("vkCreateFramebuffer Gauss y blurring error %d\n", res);
    }

    //-------------------------------------------------------//
    // FBO for bloom effect
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        std::array<VkImageView, 2> attachments = {_colorBuffer.colorBufferImageView[i], _bloomBuffer[0].colorBufferImageView[i]};

        VkFramebufferCreateInfo fbCreateInfo = {};
        fbCreateInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbCreateInfo.renderPass = m_renderPassBloom;
        fbCreateInfo.attachmentCount = attachments.size();
        fbCreateInfo.pAttachments = attachments.data();
        fbCreateInfo.width = _width;
        fbCreateInfo.height = _height;
        fbCreateInfo.layers = 1;

        res = vkCreateFramebuffer(_core.getDevice(), &fbCreateInfo, nullptr, &m_fbsBloom[i]);
        CHECK_VULKAN_ERROR("vkCreateFramebuffer Bloom error %d\n", res);
    }

    //-------------------------------------------------------//
    // FBO FXAA
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
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
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
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
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
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
    // FBO DEPTH PASS AND VIEW SPACE POS (for SSAO)
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        std::array<VkImageView, 2> attachments = {_depthBuffer.depthImageView, _viewSpaceBuffer.colorBufferImageView[i]};

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
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
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
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        std::array<VkImageView, 2> attachments = {_colorBuffer.colorBufferImageView[i], _shadingBuffer.colorBufferImageView[i]};

        VkFramebufferCreateInfo fbCreateInfo = {};
        fbCreateInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbCreateInfo.renderPass = m_renderPassSSAOblur;
        fbCreateInfo.attachmentCount = attachments.size();
        fbCreateInfo.pAttachments = attachments.data();
        fbCreateInfo.width = _width;
        fbCreateInfo.height = _height;
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

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
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

    // this initializes imgui for Vulkan
    ImGui_ImplVulkan_InitInfo init_info = {};
    init_info.Instance = _core.getInstance();
    init_info.PhysicalDevice = _core.getPhysDevice();
    init_info.Device = _core.getDevice();
    init_info.Queue = _queue;
    init_info.DescriptorPool = mImguiPool;
    init_info.MinImageCount = MAX_FRAMES_IN_FLIGHT;
    init_info.ImageCount = MAX_FRAMES_IN_FLIGHT;
    init_info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    init_info.RenderPass = m_renderPassFXAA;

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

    Utils::VulkanCreateImage(_core.getDevice(), _core.getPhysDevice(), _shadowMapBuffer.width, _shadowMapBuffer.height,
                             _shadowMapBuffer.depthFormat, VK_IMAGE_TILING_OPTIMAL,
                             VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT,
                             VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, _shadowMapBuffer.depthImage, _shadowMapBuffer.depthImageMemory);
    Utils::VulkanCreateImageView(_core.getDevice(), _shadowMapBuffer.depthImage, _shadowMapBuffer.depthFormat,
                                 VK_IMAGE_ASPECT_DEPTH_BIT, _shadowMapBuffer.depthImageView);

    Utils::VulkanCreateImage(_core.getDevice(), _core.getPhysDevice(), _depthBuffer.width, _depthBuffer.height,
                             _depthBuffer.depthFormat, VK_IMAGE_TILING_OPTIMAL,
                             VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT,
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
                             VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT,
                             VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, _footprintBuffer.depthImage, _footprintBuffer.depthImageMemory);
    Utils::VulkanCreateImageView(_core.getDevice(), _footprintBuffer.depthImage, _footprintBuffer.depthFormat,
                                 VK_IMAGE_ASPECT_DEPTH_BIT, _footprintBuffer.depthImageView);
}

void VulkanRenderer::init() {
    _core.init();
    // Get properties of our new device
    vkGetPhysicalDeviceProperties(_core.getPhysDevice(), &mDeviceProperties);

    _queue = _core.getAllQueues().at(VulkanCore::Queue_family::GFX_QUEUE_FAMILY).queue;
    if (!_queue) {
        Utils::printLog(ERROR_PARAM, "failed to get graphics queue!");
        return;
    }

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
