#pragma once

#define GLM_FORCE_RADIANS
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <array>

#include "VulkanCore.h"


class VulkanRenderer
{
public:

    static constexpr int16_t MAX_FRAMES_IN_FLIGHT = 2;
    static constexpr int16_t MAX_OBJECTS = 1;
    static constexpr std::string_view TEXTURE_FILE_NAME{ "texture.jpg" };

    struct Vertex {
        glm::vec3 pos;
        glm::vec3 color;
        glm::vec2 texCoord;

        static VkVertexInputBindingDescription getBindingDescription()
        {
            VkVertexInputBindingDescription bindingDescription{};
            bindingDescription.binding = 0;
            bindingDescription.stride = sizeof(Vertex);
            bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

            return bindingDescription;
        }

        static std::array<VkVertexInputAttributeDescription, 3> getAttributeDescriptions()
        {

            static std::array<VkVertexInputAttributeDescription, 3> attributeDescriptions{};

            attributeDescriptions[0].binding = 0;
            attributeDescriptions[0].location = 0;
            attributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
            attributeDescriptions[0].offset = offsetof(Vertex, pos);

            attributeDescriptions[1].binding = 0;
            attributeDescriptions[1].location = 1;
            attributeDescriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
            attributeDescriptions[1].offset = offsetof(Vertex, color);

            attributeDescriptions[2].binding = 0;
            attributeDescriptions[2].location = 2;
            attributeDescriptions[2].format = VK_FORMAT_R32G32_SFLOAT;
            attributeDescriptions[2].offset = offsetof(Vertex, texCoord);

            return attributeDescriptions;
        }
    };

    struct UniformBufferObject {
        alignas(16) glm::mat4 view;
        alignas(16) glm::mat4 proj;
    };

    struct DynamicUniformBufferObject {
        alignas(16) glm::mat4 model;
    };

    struct PushConstant {
        alignas(16) glm::mat4 model;
    } _pushConstant;

    const std::vector<Vertex> vertices = {
        {{-0.7f, 0.7f, 0.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f}},
        {{0.7f, 0.7f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f}},
        {{-0.7f, -0.7f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}},
        {{0.7f, -0.7f, 0.0f}, {0.0f, 1.0f, 0.0f}, {1.0f, 0.0f}}
    };

    const std::vector<uint16_t> indices = {
        0, 1, 2, 3
    };


    VulkanRenderer(std::wstring_view appName, size_t width, size_t height)
        : m_width(width)
        , m_height(height)
        ,
#ifdef WIN32
        m_core(std::make_unique<Win32Control>(appName, width, height))
#else            
        ///TO DO
#endif 
    {
    }

    ~VulkanRenderer();

    void init();

    inline VulkanCore& getVulkanCore() { return m_core; };

    void renderScene();

private:

    void createSwapChain();
    void createUniformBuffers();
    void createVertexBuffer();
    void createIndexBuffer();
    void createCommandBuffer();
    void createTextureImage();
    void createTextureImageView();
    void createTextureSampler();
    void updateUniformBuffer(uint32_t currentImage);
    void createRenderPass();
    void createDescriptorSetLayout();
    void createPushConstantRange();
    void allocateDynamicBufferTransferSpace();
    void createDescriptorPool();
    void createDescriptorSets();
    void createFramebuffer();
    void createShaders();
    void createPipeline();
    void recordCommandBuffers(uint32_t currentImage);
    void createSemaphores();
    void createDepthResources();

    size_t m_currentFrame = 0;

    int16_t m_width;
    int16_t m_height;
    VulkanCore m_core;
    std::vector<VkImage> m_images;
    VkSwapchainKHR m_swapChainKHR;
    VkQueue m_queue;
    std::vector<VkCommandBuffer> m_cmdBufs;
    VkCommandPool m_cmdBufPool;
    VkImage m_textureImage;
    VkImageView m_textureImageView;
    VkSampler m_textureSampler;
    VkDeviceMemory m_textureImageMemory;
    std::vector<VkImageView> m_views;
    VkRenderPass m_renderPass;
    VkDescriptorSetLayout m_descriptorSetLayout;
    VkPushConstantRange m_pushConstantRange;
    VkDescriptorPool m_descriptorPool;
    std::vector<VkDescriptorSet> m_descriptorSets;
    std::vector<VkFramebuffer> m_fbs;
    VkShaderModule m_vsModule;
    VkShaderModule m_fsModule;
    VkPipeline m_pipeline;
    VkPipelineLayout m_pipelineLayout;

    std::vector<VkSemaphore> m_presentCompleteSem;
    std::vector<VkSemaphore> m_renderCompleteSem;
    std::vector<VkFence> m_drawFences;

    VkBuffer m_vertexBuffer;
    VkDeviceMemory m_vertexBufferMemory;
    VkBuffer m_indexBuffer;
    VkDeviceMemory m_indexBufferMemory;
    std::vector<VkBuffer> m_uniformBuffers;
    std::vector<VkDeviceMemory> m_uniformBuffersMemory;
    std::vector<VkBuffer> m_dynamicUniformBuffers;
    std::vector<VkDeviceMemory> m_dynamicUniformBuffersMemory;
    size_t m_modelUniformAlignment;
    DynamicUniformBufferObject* mp_modelTransferSpace = nullptr;

    VkFormat m_depthFormat = VK_FORMAT_UNDEFINED;
    VkImage m_depthImage;
    VkDeviceMemory m_depthImageMemory;
    VkImageView m_depthImageView;
};

