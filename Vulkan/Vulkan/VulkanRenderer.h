#pragma once

#define GLM_FORCE_RADIANS
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <array>

#include "VulkanState.h"
#include "PipelineCreatorBase.h"
#include "I3DModel.h"

class VulkanRenderer: public VulkanState
{
public:
    static constexpr uint16_t MAX_OBJECTS = 1;
    static constexpr std::string_view MODEL_PATH{"Tank.obj"};

    struct UniformBufferObject
    {
        alignas(16) glm::mat4 view;
        alignas(16) glm::mat4 proj;
    };

    struct PushConstant
    {
        alignas(16) glm::mat4 model;
    } _pushConstant;

    VulkanRenderer(std::wstring_view appName, size_t width, size_t height);

    ~VulkanRenderer();

    void init();

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

private:
    uint16_t m_currentFrame = 0u;

    VkRenderPass m_renderPass;
    VkPushConstantRange m_pushConstantRange;
    VkDescriptorPool m_descriptorPool;
    uint16_t m_materialId = 0u;
    std::unordered_map<uint16_t, I3DModel::Material> m_descriptorSets;
    std::function<uint16_t(std::weak_ptr<TextureFactory::Texture>, VkSampler, VkDescriptorSetLayout)> m_descriptorCreator = nullptr;
    std::function<void(const PipelineCreatorBase::descriptor_set_layout_ptr&, VkDescriptorPool, std::vector<VkDescriptorSet>&, bool isDepthNeeded)> m_descriptorSecondPassCreator = nullptr;
    std::vector<VkFramebuffer> m_fbs;

    std::vector<std::unique_ptr<PipelineCreatorBase>> m_pipelineCreators;
    std::vector<std::unique_ptr<I3DModel>> m_models;

    std::vector<VkSemaphore> m_presentCompleteSem;
    std::vector<VkSemaphore> m_renderCompleteSem;
    std::vector<VkFence> m_drawFences;

    std::vector<VkBuffer> m_uniformBuffers;
    std::vector<VkDeviceMemory> m_uniformBuffersMemory;
    std::vector<VkBuffer> m_dynamicUniformBuffers;
    std::vector<VkDeviceMemory> m_dynamicUniformBuffersMemory;
    size_t m_modelUniformAlignment;
    I3DModel::DynamicUniformBufferObject *mp_modelTransferSpace = nullptr;

    std::vector<VkDescriptorSet> m_descriptorSetsSecondPass;
    VkDescriptorPool m_descriptorPoolSecondPass;

    std::vector<VkDescriptorSet> m_descriptorSetsFXAApass;
    VkDescriptorPool m_descriptorPoolFXAApass;
    std::unique_ptr<PipelineCreatorBase> m_pipelineFXAA;
    VkRenderPass m_renderPassFXAA;
    std::vector<VkFramebuffer> m_fbsFXAA;

    /// smart ptr for taking over responsibility for lazy init and early removal
    std::unique_ptr<TextureFactory> mTextureFactory;
};