#pragma once

#define GLM_FORCE_RADIANS
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <array>

#include "VulkanCore.h"
#include "ObjModel.h"

class VulkanRenderer
{
public:
    /// <summary>
    /// Using double buffering and vsync locks rendering to an integer fraction of the vsync rate.
    /// In turn, reducing the performance of the application if rendering is slower than vsync.
    /// Consider setting minImageCount to 3 to use triple buffering to maximize performance in such cases.
    /// </summary>
    static constexpr uint16_t MAX_FRAMES_IN_FLIGHT = 3; /// tripple buffering is the best choice
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

    inline VulkanCore &getVulkanCore() { return m_core; };

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
    void createDescriptorSetLayout();
    void createPushConstantRange();
    void allocateDynamicBufferTransferSpace();
    void createDescriptorPool();
    void createDescriptorSetsSecondPass();
    void createFramebuffer();
    void createShaders();
    void createPipeline();
    void recordCommandBuffers(uint32_t currentImage);
    void createSemaphores();
    void createDepthResources();
    void createColourBufferImage();
    void loadModels();
    void recreateDescriptorSets();

    size_t m_currentFrame = 0;

    uint16_t m_width;
    uint16_t m_height;
    VulkanCore m_core;
    std::vector<VkImage> m_images;
    VkSwapchainKHR m_swapChainKHR;
    VkQueue m_queue;
    std::vector<VkCommandBuffer> m_cmdBufs;
    VkCommandPool m_cmdBufPool;
    std::vector<VkImageView> m_views;
    VkRenderPass m_renderPass;
    VkDescriptorSetLayout m_descriptorSetLayout;
    VkPushConstantRange m_pushConstantRange;
    VkDescriptorPool m_descriptorPool;
    uint16_t m_materialId = 0u;
    std::unordered_map<uint16_t, I3DModel::Material> m_descriptorSets;
    std::function<uint16_t(std::weak_ptr<TextureFactory::Texture>, VkSampler)> m_descriptorCreator = nullptr;
    std::vector<VkFramebuffer> m_fbs;
    VkShaderModule m_vsModule;
    VkShaderModule m_fsModule;
    VkPipeline m_pipeline;
    VkPipelineLayout m_pipelineLayout;

    std::vector<VkSemaphore> m_presentCompleteSem;
    std::vector<VkSemaphore> m_renderCompleteSem;
    std::vector<VkFence> m_drawFences;

    ObjModel m_objModel;
    std::vector<VkBuffer> m_uniformBuffers;
    std::vector<VkDeviceMemory> m_uniformBuffersMemory;
    std::vector<VkBuffer> m_dynamicUniformBuffers;
    std::vector<VkDeviceMemory> m_dynamicUniformBuffersMemory;
    size_t m_modelUniformAlignment;
    I3DModel::DynamicUniformBufferObject *mp_modelTransferSpace = nullptr;

    VkFormat m_depthFormat = VK_FORMAT_UNDEFINED;
    VkImage m_depthImage;
    VkDeviceMemory m_depthImageMemory;
    VkImageView m_depthImageView;

    VkFormat m_colourFormat = VK_FORMAT_UNDEFINED;
    VkDescriptorSetLayout m_descriptorSetLayoutSecondPass;
    std::vector<VkDescriptorSet> m_descriptorSetsSecondPass;
    VkDescriptorPool m_descriptorPoolSecondPass;
    std::vector<VkImage> m_colourBufferImage;
    std::vector<VkDeviceMemory> m_colourBufferImageMemory;
    std::vector<VkImageView> m_colourBufferImageView;
    VkShaderModule m_vsModuleSecondPass;
    VkShaderModule m_fsModuleSecondPass;
    VkPipeline m_pipelineSecondPass;
    VkPipelineLayout m_pipelineLayoutSecondPass;
};