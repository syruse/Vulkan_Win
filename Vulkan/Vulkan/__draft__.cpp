/*

    Copyright 2017 Etay Meiri

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.

    Tutorial 53 - Semaphores and other fixes
*/
#include <cfloat>
#include <math.h>
#include <GL/glew.h>
#include <string>
#ifndef WIN32
#include <sys/time.h>
#include <unistd.h>
#endif
#include <sys/types.h>
#include <array>
#include <vector>
#include <stdexcept>
#include <chrono>

#include "ogldev_engine_common.h"
#include "ogldev_app.h"
#include "ogldev_util.h"
#include "ogldev_vulkan_core.h"
#include "ogldev_math_3d.h"
#include "ogldev_win32_control.h"

#define GLM_FORCE_RADIANS
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>


static constexpr int16_t WINDOW_WIDTH = 1024;
static constexpr int16_t WINDOW_HEIGHT = 1024;
static constexpr int16_t MAX_FRAMES_IN_FLIGHT = 2;
static constexpr int16_t MAX_OBJECTS = 1;

struct Vertex {
    glm::vec3 pos;
    glm::vec3 color;

    static VkVertexInputBindingDescription getBindingDescription() 
    {
        VkVertexInputBindingDescription bindingDescription{};
        bindingDescription.binding = 0;
        bindingDescription.stride = sizeof(Vertex);
        bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

        return bindingDescription;
    }

    static std::array<VkVertexInputAttributeDescription, 2> getAttributeDescriptions() 
    {

        static std::array<VkVertexInputAttributeDescription, 2> attributeDescriptions{};

        attributeDescriptions[0].binding = 0;
        attributeDescriptions[0].location = 0;
        attributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
        attributeDescriptions[0].offset = offsetof(Vertex, pos);

        attributeDescriptions[1].binding = 0;
        attributeDescriptions[1].location = 1;
        attributeDescriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
        attributeDescriptions[1].offset = offsetof(Vertex, color);

        return attributeDescriptions;
    }
};

size_t findMemoryType(const VkPhysicalDevice& physicalDevice, const VkMemoryRequirements& memRequirements, VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);

    for (size_t i = 0u; i < memProperties.memoryTypeCount; i++) {
        if ((memRequirements.memoryTypeBits & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }

    throw std::runtime_error("failed to find suitable memory type!");
}

void createBuffer(const VkDevice& device, const VkPhysicalDevice& physicalDevice, VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer& buffer, VkDeviceMemory& bufferMemory) {
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(device, &bufferInfo, nullptr, &buffer) != VK_SUCCESS) {
        throw std::runtime_error("failed to create buffer!");
    }

    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(device, buffer, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = findMemoryType(physicalDevice, memRequirements, properties);

    if (vkAllocateMemory(device, &allocInfo, nullptr, &bufferMemory) != VK_SUCCESS) {
        throw std::runtime_error("failed to allocate buffer memory!");
    }

    vkBindBufferMemory(device, buffer, bufferMemory, 0);
}

void copyBuffer(VkDevice device, VkQueue queue, VkCommandPool cmdBufPool, VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size) 
{
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = cmdBufPool;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer commandBuffer;
    vkAllocateCommandBuffers(device, &allocInfo, &commandBuffer);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkBeginCommandBuffer(commandBuffer, &beginInfo);

    VkBufferCopy copyRegion{};
    copyRegion.size = size;
    vkCmdCopyBuffer(commandBuffer, srcBuffer, dstBuffer, 1, &copyRegion);

    vkEndCommandBuffer(commandBuffer);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(queue);

    vkFreeCommandBuffers(device, cmdBufPool, 1, &commandBuffer);
}

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
    {{-0.7f, 0.7f, 0.0f}, {1.0f, 0.0f, 0.0f}},
    {{0.7f, 0.7f, 0.0f}, {0.0f, 0.0f, 1.0f}},
    {{-0.7f, -0.7f, 0.0f}, {0.0f, 0.0f, 1.0f}},
    {{0.7f, -0.7f, 0.0f}, {0.0f, 1.0f, 0.0f}}
};

const std::vector<uint16_t> indices = {
    0, 1, 2, 3
};


class OgldevVulkanApp
{
public:

    OgldevVulkanApp(const char* pAppName);

    ~OgldevVulkanApp();

    void Init();

    void Run();

private:

    void CreateSwapChain();
    void CreateUniformBuffers();
    void CreateVertexBuffer();
    void CreateIndexBuffer();
    void CreateCommandBuffer();
    void UpdateUniformBuffer(uint32_t currentImage);
    void CreateRenderPass();
    void CreateDescriptorSetLayout();
    void CreatePushConstantRange();
    void AllocateDynamicBufferTransferSpace();
    void CreateDescriptorPool();
    void CreateDescriptorSets();
    void CreateFramebuffer();
    void CreateShaders();
    void CreatePipeline();
    void RecordCommandBuffers(uint32_t currentImage);
    void CreateSemaphores();
    void RenderScene();

    size_t m_currentFrame = 0;

    std::string m_appName;
    VulkanWindowControl* m_pWindowControl;
    OgldevVulkanCore m_core;
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
    DynamicUniformBufferObject* m_modelTransferSpace;
};


OgldevVulkanApp::OgldevVulkanApp(const char* pAppName) : m_core(pAppName)
{
    m_appName = std::string(pAppName);
}


OgldevVulkanApp::~OgldevVulkanApp()
{
    // Wait until no actions being run on device before destroying
    vkDeviceWaitIdle(m_core.GetDevice());

    _aligned_free(m_modelTransferSpace);

    vkDestroyBuffer(m_core.GetDevice(), m_vertexBuffer, nullptr);
    vkFreeMemory(m_core.GetDevice(), m_vertexBufferMemory, nullptr);
    vkDestroyBuffer(m_core.GetDevice(), m_indexBuffer, nullptr);
    vkFreeMemory(m_core.GetDevice(), m_indexBufferMemory, nullptr);
    for (size_t i = 0; i < m_images.size(); i++) {
        vkDestroyBuffer(m_core.GetDevice(), m_uniformBuffers[i], nullptr);
        vkFreeMemory(m_core.GetDevice(), m_uniformBuffersMemory[i], nullptr);
        vkDestroyBuffer(m_core.GetDevice(), m_dynamicUniformBuffers[i], nullptr);
        vkFreeMemory(m_core.GetDevice(), m_dynamicUniformBuffersMemory[i], nullptr);
    }

    for (auto framebuffer : m_fbs) {
        vkDestroyFramebuffer(m_core.GetDevice(), framebuffer, nullptr);
    }

    for (auto imageView : m_views) {
        vkDestroyImageView(m_core.GetDevice(), imageView, nullptr);
    }

    vkDestroySwapchainKHR(m_core.GetDevice(), m_swapChainKHR, nullptr);

    vkDestroyDescriptorPool(m_core.GetDevice(), m_descriptorPool, nullptr);

    vkDestroyDescriptorSetLayout(m_core.GetDevice(), m_descriptorSetLayout, nullptr);

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
    {
        vkDestroySemaphore(m_core.GetDevice(), m_presentCompleteSem[i], nullptr);
        vkDestroySemaphore(m_core.GetDevice(), m_renderCompleteSem[i], nullptr);
        vkDestroyFence(m_core.GetDevice(), m_drawFences[i], nullptr);
    }

    vkDestroyCommandPool(m_core.GetDevice(), m_cmdBufPool, nullptr);
    vkDestroyDevice(m_core.GetDevice(), nullptr);
    vkDestroySurfaceKHR(m_core.GetInstance(), m_core.GetSurface(), nullptr);
    vkDestroyInstance(m_core.GetInstance(), nullptr);
}

void OgldevVulkanApp::CreatePushConstantRange()
{
    // Define push constant values (no 'create' needed!)
    m_pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;	// Shader stage push constant will go to
    m_pushConstantRange.offset = 0;								// Offset into given data to pass to push constant
    m_pushConstantRange.size = sizeof(PushConstant);						// Size of data being passed
}

void OgldevVulkanApp::UpdateUniformBuffer(uint32_t currentImage) {

    assert(m_uniformBuffersMemory.size() > currentImage);

    static auto startTime = std::chrono::high_resolution_clock::now();

    auto currentTime = std::chrono::high_resolution_clock::now();
    float time = std::chrono::duration<float, std::chrono::seconds::period>(currentTime - startTime).count();

    UniformBufferObject ubo{};
    ubo.view = glm::lookAt(glm::vec3(2.0f, 2.0f, 2.0f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    ubo.proj = glm::perspective(glm::radians(65.0f), WINDOW_WIDTH / (float)WINDOW_HEIGHT, 0.01f, 1000.0f);

    /**
    * GLM was originally designed for OpenGL, where the Y coordinate of the clip coordinates is inverted
    * The easiest way to compensate for that is to flip the sign on the scaling factor of the Y axis in the projection matrix
    */
    ubo.proj[1][1] *= -1;

    // Copy VP data
    void* data;
    vkMapMemory(m_core.GetDevice(), m_uniformBuffersMemory[currentImage], 0, sizeof(ubo), 0, &data);
    memcpy(data, &ubo, sizeof(ubo));
    vkUnmapMemory(m_core.GetDevice(), m_uniformBuffersMemory[currentImage]);

    // Copy Model data
    for (size_t i = 0; i < MAX_OBJECTS; i++)
    {
        DynamicUniformBufferObject* pModel = (DynamicUniformBufferObject*)((uint64_t)m_modelTransferSpace + (i * m_modelUniformAlignment));
        pModel->model = glm::rotate(glm::mat4(1.0f), time * glm::radians(90.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    }

    // Map the list of model data
    vkMapMemory(m_core.GetDevice(), m_dynamicUniformBuffersMemory[currentImage], 0, m_modelUniformAlignment * MAX_OBJECTS, 0, &data);
    memcpy(data, m_modelTransferSpace, m_modelUniformAlignment * MAX_OBJECTS);
    vkUnmapMemory(m_core.GetDevice(), m_dynamicUniformBuffersMemory[currentImage]);
}

void OgldevVulkanApp::AllocateDynamicBufferTransferSpace()
{
    // Get properties of our new device
    VkPhysicalDeviceProperties deviceProperties;
    vkGetPhysicalDeviceProperties(m_core.GetPhysDevice(), &deviceProperties);

    size_t minUniformBufferOffset = static_cast<size_t>(deviceProperties.limits.minUniformBufferOffsetAlignment);

    // Calculate alignment of model data
    m_modelUniformAlignment = (sizeof(DynamicUniformBufferObject) + minUniformBufferOffset - 1)
        & ~(minUniformBufferOffset - 1);

    // Create space in memory to hold dynamic buffer that is aligned to our required alignment and holds MAX_OBJECTS
    m_modelTransferSpace = (DynamicUniformBufferObject*)_aligned_malloc(m_modelUniformAlignment * MAX_OBJECTS, m_modelUniformAlignment);
}

void OgldevVulkanApp::CreateDescriptorSetLayout()
{
    // UboViewProjection Binding Info
    VkDescriptorSetLayoutBinding uboLayoutBinding{};
    uboLayoutBinding.binding = 0;
    uboLayoutBinding.descriptorCount = 1;
    uboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uboLayoutBinding.pImmutableSamplers = nullptr;
    uboLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    // Model Binding Info
    VkDescriptorSetLayoutBinding modelLayoutBinding = {};
    modelLayoutBinding.binding = 1;
    modelLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    modelLayoutBinding.descriptorCount = 1;
    modelLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    modelLayoutBinding.pImmutableSamplers = nullptr;

    std::vector<VkDescriptorSetLayoutBinding> layoutBindings = { uboLayoutBinding, modelLayoutBinding };
    // Create Descriptor Set Layout with given bindings
    VkDescriptorSetLayoutCreateInfo layoutCreateInfo = {};
    layoutCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutCreateInfo.bindingCount = static_cast<uint32_t>(layoutBindings.size());;
    layoutCreateInfo.pBindings = layoutBindings.data();

    if (vkCreateDescriptorSetLayout(m_core.GetDevice(), &layoutCreateInfo, nullptr, &m_descriptorSetLayout) != VK_SUCCESS) {
        throw std::runtime_error("failed to create descriptor set layout!");
    }
}

void OgldevVulkanApp::CreateDescriptorPool() 
{
    // Type of descriptors + how many DESCRIPTORS, not Descriptor Sets (combined makes the pool size)
    // ViewProjection Pool
    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSize.descriptorCount = static_cast<uint32_t>(m_images.size());

    // Model Pool (DYNAMIC)
    VkDescriptorPoolSize modelPoolSize = {};
    modelPoolSize.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    modelPoolSize.descriptorCount = static_cast<uint32_t>(m_images.size());

    // List of pool sizes
    std::vector<VkDescriptorPoolSize> descriptorPoolSizes = { poolSize, modelPoolSize };

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = static_cast<uint32_t>(descriptorPoolSizes.size());		// Amount of Pool Sizes being passed
    poolInfo.pPoolSizes = descriptorPoolSizes.data();                               // Pool Sizes to create pool with
    poolInfo.maxSets = static_cast<uint32_t>(m_images.size());					    // Maximum number of Descriptor Sets that can be created from pool

    if (vkCreateDescriptorPool(m_core.GetDevice(), &poolInfo, nullptr, &m_descriptorPool) != VK_SUCCESS) {
        throw std::runtime_error("failed to create descriptor pool!");
    }
}

void OgldevVulkanApp::CreateDescriptorSets() 
{
    std::vector<VkDescriptorSetLayout> layouts(m_images.size(), m_descriptorSetLayout);
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = m_descriptorPool;
    allocInfo.descriptorSetCount = static_cast<uint32_t>(m_images.size());
    allocInfo.pSetLayouts = layouts.data();

    m_descriptorSets.resize(m_images.size());
    if (vkAllocateDescriptorSets(m_core.GetDevice(), &allocInfo, m_descriptorSets.data()) != VK_SUCCESS) {
        throw std::runtime_error("failed to allocate descriptor sets!");
    }

    // connect the descriptors with buffer when binding
    for (size_t i = 0; i < m_images.size(); i++) {
        // VIEW PROJECTION DESCRIPTOR
        // Buffer info and data offset info
        VkDescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = m_uniformBuffers[i];
        bufferInfo.offset = 0;
        bufferInfo.range = sizeof(UniformBufferObject);

        VkWriteDescriptorSet descriptorWrite{};
        descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrite.dstSet = m_descriptorSets[i];
        descriptorWrite.dstBinding = 0;
        descriptorWrite.dstArrayElement = 0;
        descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        descriptorWrite.descriptorCount = 1;
        descriptorWrite.pBufferInfo = &bufferInfo;
        descriptorWrite.pImageInfo = nullptr; // Optional
        descriptorWrite.pTexelBufferView = nullptr; // Optional

        // MODEL DESCRIPTOR
        // Model Buffer Binding Info
        VkDescriptorBufferInfo modelBufferInfo = {};
        modelBufferInfo.buffer = m_dynamicUniformBuffers[i];
        modelBufferInfo.offset = 0;
        modelBufferInfo.range = m_modelUniformAlignment;

        VkWriteDescriptorSet modelSetWrite = {};
        modelSetWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        modelSetWrite.dstSet = m_descriptorSets[i];
        modelSetWrite.dstBinding = 1;
        modelSetWrite.dstArrayElement = 0;
        modelSetWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
        modelSetWrite.descriptorCount = 1;
        modelSetWrite.pBufferInfo = &modelBufferInfo;

        // List of Descriptor Set Writes
        std::vector<VkWriteDescriptorSet> setWrites = { descriptorWrite, modelSetWrite };

        // Update the descriptor sets with new buffer/binding info
        vkUpdateDescriptorSets(m_core.GetDevice(), static_cast<uint32_t>(setWrites.size()), setWrites.data(),
            0, nullptr);
    }
}


void OgldevVulkanApp::CreateSwapChain()
{
    const VkSurfaceCapabilitiesKHR& SurfaceCaps = m_core.GetSurfaceCaps();

    assert(SurfaceCaps.currentExtent.width != -1);

    assert(MAX_FRAMES_IN_FLIGHT >= SurfaceCaps.minImageCount);
    assert(MAX_FRAMES_IN_FLIGHT <= SurfaceCaps.maxImageCount);

    VkSwapchainCreateInfoKHR SwapChainCreateInfo = {};

    SwapChainCreateInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    SwapChainCreateInfo.surface = m_core.GetSurface();
    SwapChainCreateInfo.minImageCount = MAX_FRAMES_IN_FLIGHT;
    SwapChainCreateInfo.imageFormat = m_core.GetSurfaceFormat().format;
    SwapChainCreateInfo.imageColorSpace = m_core.GetSurfaceFormat().colorSpace;
    SwapChainCreateInfo.imageExtent = SurfaceCaps.currentExtent;
    SwapChainCreateInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    SwapChainCreateInfo.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    SwapChainCreateInfo.imageArrayLayers = 1;
    SwapChainCreateInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    SwapChainCreateInfo.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    SwapChainCreateInfo.clipped = VK_TRUE;
    SwapChainCreateInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;

    VkResult res = vkCreateSwapchainKHR(m_core.GetDevice(), &SwapChainCreateInfo, NULL, &m_swapChainKHR);
    CHECK_VULKAN_ERROR("vkCreateSwapchainKHR error %d\n", res);

    printf("Swap chain created\n");

    uint NumSwapChainImages = 0;
    res = vkGetSwapchainImagesKHR(m_core.GetDevice(), m_swapChainKHR, &NumSwapChainImages, NULL);
    CHECK_VULKAN_ERROR("vkGetSwapchainImagesKHR error %d\n", res);
    assert(MAX_FRAMES_IN_FLIGHT == NumSwapChainImages);
    printf("Number of images %d\n", NumSwapChainImages);

    m_images.resize(NumSwapChainImages);
    m_views.resize(NumSwapChainImages);
    m_cmdBufs.resize(NumSwapChainImages);

    res = vkGetSwapchainImagesKHR(m_core.GetDevice(), m_swapChainKHR, &NumSwapChainImages, &(m_images[0]));
    CHECK_VULKAN_ERROR("vkGetSwapchainImagesKHR error %d\n", res);
}
/** deprecated
void OgldevVulkanApp::CreateVertexBuffer()
{
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = sizeof(vertices[0]) * vertices.size();
    bufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VkResult res = vkCreateBuffer(m_core.GetDevice(), &bufferInfo, nullptr, &m_vertexBuffer);
    CHECK_VULKAN_ERROR("vkCreateBuffer error %d\n", res);

    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(m_core.GetDevice(), m_vertexBuffer, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = findMemoryType(m_core.GetPhysDevice(), memRequirements, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    res = vkAllocateMemory(m_core.GetDevice(), &allocInfo, nullptr, &m_vertexBufferMemory);
    CHECK_VULKAN_ERROR("vkAllocateMemory error %d\n", res);

    vkBindBufferMemory(m_core.GetDevice(), m_vertexBuffer, m_vertexBufferMemory, 0);

    void* data;
    res = vkMapMemory(m_core.GetDevice(), m_vertexBufferMemory, 0, bufferInfo.size, 0, &data);
    CHECK_VULKAN_ERROR("vkMapMemory error %d\n", res);
    memcpy(data, vertices.data(), (size_t)bufferInfo.size);
    vkUnmapMemory(m_core.GetDevice(), m_vertexBufferMemory);
}*/

void OgldevVulkanApp::CreateUniformBuffers() {
    // ViewProjection buffer size
    VkDeviceSize bufferSize = sizeof(UniformBufferObject);

    // Model buffer size
    VkDeviceSize modelBufferSize = m_modelUniformAlignment * MAX_OBJECTS;

    m_uniformBuffers.resize(m_images.size());
    m_uniformBuffersMemory.resize(m_images.size());
    m_dynamicUniformBuffers.resize(m_images.size());
    m_dynamicUniformBuffersMemory.resize(m_images.size());

    /**
    * We should have multiple buffers, because multiple frames may be in flight at the same time and 
    * we don't want to update the buffer in preparation of the next frame while a previous one is still reading from it! 
    */
    for (size_t i = 0; i < m_images.size(); i++) {
        createBuffer(m_core.GetDevice(), m_core.GetPhysDevice(), bufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, 
            m_uniformBuffers[i], m_uniformBuffersMemory[i]);
        createBuffer(m_core.GetDevice(), m_core.GetPhysDevice(), modelBufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            m_dynamicUniformBuffers[i], m_dynamicUniformBuffersMemory[i]);
    }

    /// uniform buffer updating with a new transformation occurs every frame, so there will be no vkMapMemory here
}

void OgldevVulkanApp::CreateVertexBuffer()
{
    VkDeviceSize bufferSize = sizeof(vertices[0]) * vertices.size();

    VkBuffer stagingBuffer;
    VkDeviceMemory stagingBufferMemory;
    createBuffer(m_core.GetDevice(), m_core.GetPhysDevice(), bufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, stagingBuffer, stagingBufferMemory);

    void* data;
    vkMapMemory(m_core.GetDevice(), stagingBufferMemory, 0, bufferSize, 0, &data);
    memcpy(data, vertices.data(), (size_t)bufferSize);
    vkUnmapMemory(m_core.GetDevice(), stagingBufferMemory);

    createBuffer(m_core.GetDevice(), m_core.GetPhysDevice(), bufferSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, m_vertexBuffer, m_vertexBufferMemory);

    copyBuffer(m_core.GetDevice(), m_queue, m_cmdBufPool, stagingBuffer, m_vertexBuffer, bufferSize);

    vkDestroyBuffer(m_core.GetDevice(), stagingBuffer, nullptr);
    vkFreeMemory(m_core.GetDevice(), stagingBufferMemory, nullptr);
}

void OgldevVulkanApp::CreateIndexBuffer() {
    VkDeviceSize bufferSize = sizeof(indices[0]) * indices.size();

    VkBuffer stagingBuffer;
    VkDeviceMemory stagingBufferMemory;
    createBuffer(m_core.GetDevice(), m_core.GetPhysDevice(), bufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, stagingBuffer, stagingBufferMemory);

    void* data;
    vkMapMemory(m_core.GetDevice(), stagingBufferMemory, 0, bufferSize, 0, &data);
    memcpy(data, indices.data(), (size_t)bufferSize);
    vkUnmapMemory(m_core.GetDevice(), stagingBufferMemory);

    createBuffer(m_core.GetDevice(), m_core.GetPhysDevice(), bufferSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, m_indexBuffer, m_indexBufferMemory);

    copyBuffer(m_core.GetDevice(), m_queue, m_cmdBufPool, stagingBuffer, m_indexBuffer, bufferSize);

    vkDestroyBuffer(m_core.GetDevice(), stagingBuffer, nullptr);
    vkFreeMemory(m_core.GetDevice(), stagingBufferMemory, nullptr);
}

void OgldevVulkanApp::CreateCommandBuffer()
{
    VkCommandPoolCreateInfo cmdPoolCreateInfo = {};
    cmdPoolCreateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    cmdPoolCreateInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT; /// comand buffer will be reset when VkBegin called, so it's resetable now
    cmdPoolCreateInfo.queueFamilyIndex = m_core.GetQueueFamily();

    VkResult res = vkCreateCommandPool(m_core.GetDevice(), &cmdPoolCreateInfo, NULL, &m_cmdBufPool);
    CHECK_VULKAN_ERROR("vkCreateCommandPool error %d\n", res);

    printf("Command buffer pool created\n");

    VkCommandBufferAllocateInfo cmdBufAllocInfo = {};
    cmdBufAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmdBufAllocInfo.commandPool = m_cmdBufPool;
    cmdBufAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmdBufAllocInfo.commandBufferCount = m_images.size();

    res = vkAllocateCommandBuffers(m_core.GetDevice(), &cmdBufAllocInfo, &m_cmdBufs[0]);
    CHECK_VULKAN_ERROR("vkAllocateCommandBuffers error %d\n", res);

    printf("Created command buffers\n");
}


void OgldevVulkanApp::RecordCommandBuffers(uint32_t currentImage)
{
    VkCommandBufferBeginInfo beginInfo = {};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;

    VkClearColorValue clearColor = { 164.0f / 256.0f, 30.0f / 256.0f, 34.0f / 256.0f, 0.0f };
    VkClearValue clearValue = {};
    clearValue.color = clearColor;

    VkImageSubresourceRange imageRange = {};
    imageRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    imageRange.levelCount = 1;
    imageRange.layerCount = 1;

    VkRenderPassBeginInfo renderPassInfo = {};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = m_renderPass;
    renderPassInfo.renderArea.offset.x = 0;
    renderPassInfo.renderArea.offset.y = 0;
    renderPassInfo.renderArea.extent.width = WINDOW_WIDTH;
    renderPassInfo.renderArea.extent.height = WINDOW_HEIGHT;
    renderPassInfo.clearValueCount = 1;
    renderPassInfo.pClearValues = &clearValue;

    //for (uint i = 0; i < m_cmdBufs.size(); i++) {
        VkResult res = vkBeginCommandBuffer(m_cmdBufs[currentImage], &beginInfo);
        CHECK_VULKAN_ERROR("vkBeginCommandBuffer error %d\n", res);
        renderPassInfo.framebuffer = m_fbs[currentImage];

        vkCmdBeginRenderPass(m_cmdBufs[currentImage], &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

        vkCmdBindPipeline(m_cmdBufs[currentImage], VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);


        /// For Each mesh start
        VkBuffer vertexBuffers[] = { m_vertexBuffer };
        VkDeviceSize offsets[] = { 0 };
        vkCmdBindVertexBuffers(m_cmdBufs[currentImage], 0, 1, vertexBuffers, offsets);
        vkCmdBindIndexBuffer(m_cmdBufs[currentImage], m_indexBuffer, 0, VK_INDEX_TYPE_UINT16);

        // Dynamic Offset Amount
        size_t meshIndex = 0;
        const uint32_t dynamicOffset = static_cast<uint32_t>(m_modelUniformAlignment) * meshIndex;

        vkCmdPushConstants(
            m_cmdBufs[currentImage], 
            m_pipelineLayout,
            VK_SHADER_STAGE_VERTEX_BIT,		// Stage to push constants to
            0,								// Offset of push constants to update
            sizeof(PushConstant),		    // Size of data being pushed
            &_pushConstant);		        // Actual data being pushed (can be array)

        /**
        * Unlike vertex and index buffers, descriptor sets are not unique to graphics pipelines. 
        * Therefore we need to specify if we want to bind descriptor sets to the graphics or compute pipeline
        */
        // Bind Descriptor Sets
        vkCmdBindDescriptorSets(m_cmdBufs[currentImage], VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout, 0, 1, &m_descriptorSets[currentImage], 1, &dynamicOffset);

        /** no need to draw over vertices vkCmdDraw(m_cmdBufs[i], static_cast<uint32_t>(vertices.size()), 1, 0, 0); */
        // Execute pipeline
        vkCmdDrawIndexed(m_cmdBufs[currentImage], static_cast<uint32_t>(indices.size()), 1, 0, 0, 0);
        /// For Each mesh end

        vkCmdEndRenderPass(m_cmdBufs[currentImage]);

        res = vkEndCommandBuffer(m_cmdBufs[currentImage]);
        CHECK_VULKAN_ERROR("vkEndCommandBuffer error %d\n", res);
    //}

    printf("Command buffers recorded\n");
}


void OgldevVulkanApp::RenderScene()
{
    // -- GET NEXT IMAGE --
    // Wait for given fence to signal (open) from last draw before continuing
    vkWaitForFences(m_core.GetDevice(), 1, &m_drawFences[m_currentFrame], VK_TRUE, std::numeric_limits<uint64_t>::max());
    // Manually reset (close) fences
    vkResetFences(m_core.GetDevice(), 1, &m_drawFences[m_currentFrame]);

    uint ImageIndex = 0;
    VkResult res = vkAcquireNextImageKHR(m_core.GetDevice(), m_swapChainKHR, UINT64_MAX, m_presentCompleteSem[m_currentFrame], NULL, &ImageIndex);
    CHECK_VULKAN_ERROR("vkAcquireNextImageKHR error %d\n", res);

    VkPipelineStageFlags waitFlags = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submitInfo = {};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &m_cmdBufs[ImageIndex];
    submitInfo.pWaitSemaphores = &m_presentCompleteSem[m_currentFrame];
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitDstStageMask = &waitFlags;
    submitInfo.pSignalSemaphores = &m_renderCompleteSem[m_currentFrame];
    submitInfo.signalSemaphoreCount = 1;

    RecordCommandBuffers(ImageIndex); /// added here since now comand buffer is reset after each vkBegin command
    UpdateUniformBuffer(ImageIndex);

    res = vkQueueSubmit(m_queue, 1, &submitInfo, m_drawFences[m_currentFrame]);
    CHECK_VULKAN_ERROR("vkQueueSubmit error %d\n", res);

    VkPresentInfoKHR presentInfo = {};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &m_swapChainKHR;
    presentInfo.pImageIndices = &ImageIndex;
    presentInfo.pWaitSemaphores = &m_renderCompleteSem[m_currentFrame];
    presentInfo.waitSemaphoreCount = 1;

    res = vkQueuePresentKHR(m_queue, &presentInfo);
    CHECK_VULKAN_ERROR("vkQueuePresentKHR error %d\n", res);

    // Get next frame (use % MAX_FRAME_DRAWS to keep value below MAX_FRAME_DRAWS)
    m_currentFrame = ++m_currentFrame % MAX_FRAMES_IN_FLIGHT;
}



void OgldevVulkanApp::CreateRenderPass()
{
    VkAttachmentReference attachRef = {};
    attachRef.attachment = 0;
    attachRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpassDesc = {};
    subpassDesc.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpassDesc.colorAttachmentCount = 1;
    subpassDesc.pColorAttachments = &attachRef;

    VkAttachmentDescription attachDesc = {};
    attachDesc.format = m_core.GetSurfaceFormat().format;
    attachDesc.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachDesc.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachDesc.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachDesc.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachDesc.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachDesc.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    attachDesc.samples = VK_SAMPLE_COUNT_1_BIT;

    VkRenderPassCreateInfo renderPassCreateInfo = {};
    renderPassCreateInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassCreateInfo.attachmentCount = 1;
    renderPassCreateInfo.pAttachments = &attachDesc;
    renderPassCreateInfo.subpassCount = 1;
    renderPassCreateInfo.pSubpasses = &subpassDesc;

    VkResult res = vkCreateRenderPass(m_core.GetDevice(), &renderPassCreateInfo, NULL, &m_renderPass);
    CHECK_VULKAN_ERROR("vkCreateRenderPass error %d\n", res);

    printf("Created a render pass\n");
}


void OgldevVulkanApp::CreateFramebuffer()
{
    m_fbs.resize(m_images.size());

    VkResult res;

    for (uint i = 0; i < m_images.size(); i++) {
        VkImageViewCreateInfo ViewCreateInfo = {};
        ViewCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        ViewCreateInfo.image = m_images[i];
        ViewCreateInfo.format = m_core.GetSurfaceFormat().format;
        ViewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        ViewCreateInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        ViewCreateInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        ViewCreateInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        ViewCreateInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
        ViewCreateInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        ViewCreateInfo.subresourceRange.baseMipLevel = 0;
        ViewCreateInfo.subresourceRange.levelCount = 1;
        ViewCreateInfo.subresourceRange.baseArrayLayer = 0;
        ViewCreateInfo.subresourceRange.layerCount = 1;

        res = vkCreateImageView(m_core.GetDevice(), &ViewCreateInfo, NULL, &m_views[i]);
        CHECK_VULKAN_ERROR("vkCreateImageView error %d\n", res);

        VkFramebufferCreateInfo fbCreateInfo = {};
        fbCreateInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbCreateInfo.renderPass = m_renderPass;
        fbCreateInfo.attachmentCount = 1;
        fbCreateInfo.pAttachments = &m_views[i];
        fbCreateInfo.width = WINDOW_WIDTH;
        fbCreateInfo.height = WINDOW_HEIGHT;
        fbCreateInfo.layers = 1;

        res = vkCreateFramebuffer(m_core.GetDevice(), &fbCreateInfo, NULL, &m_fbs[i]);
        CHECK_VULKAN_ERROR("vkCreateFramebuffer error %d\n", res);
    }

    printf("Frame buffers created\n");
}


void OgldevVulkanApp::CreateShaders()
{
    m_vsModule = VulkanCreateShaderModule(m_core.GetDevice(), "vert.spv");
    assert(m_vsModule);

    m_fsModule = VulkanCreateShaderModule(m_core.GetDevice(), "frag.spv");
    assert(m_fsModule);
}


void OgldevVulkanApp::CreateSemaphores()
{
    //m_presentCompleteSem = m_core.CreateSemaphore();
    //m_renderCompleteSem = m_core.CreateSemaphore();

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
        if (vkCreateSemaphore(m_core.GetDevice(), &semaphoreCreateInfo, nullptr, &m_presentCompleteSem[i]) != VK_SUCCESS ||
            vkCreateSemaphore(m_core.GetDevice(), &semaphoreCreateInfo, nullptr, &m_renderCompleteSem[i]) != VK_SUCCESS ||
            vkCreateFence(m_core.GetDevice(), &fenceCreateInfo, nullptr, &m_drawFences[i]) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create a Semaphore and/or Fence!");
        }
    }
}


#define VERTEX_BUFFER_BIND_ID 0

void OgldevVulkanApp::CreatePipeline()
{
    VkPipelineShaderStageCreateInfo shaderStageCreateInfo[2] = {};

    shaderStageCreateInfo[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStageCreateInfo[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    shaderStageCreateInfo[0].module = m_vsModule;
    shaderStageCreateInfo[0].pName = "main";
    shaderStageCreateInfo[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStageCreateInfo[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    shaderStageCreateInfo[1].module = m_fsModule;
    shaderStageCreateInfo[1].pName = "main";

    VkPipelineVertexInputStateCreateInfo vertexInputInfo = {};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    auto bindingDescription = Vertex::getBindingDescription();
    auto attributeDescriptions = Vertex::getAttributeDescriptions();

    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
    vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
    vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

    VkPipelineInputAssemblyStateCreateInfo pipelineIACreateInfo = {};
    pipelineIACreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    pipelineIACreateInfo.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;

    VkViewport vp = {};
    vp.x = 0.0f;
    vp.y = 0.0f;
    vp.width = (float)WINDOW_WIDTH;
    vp.height = (float)WINDOW_HEIGHT;
    vp.minDepth = 0.0f;
    vp.maxDepth = 1.0f;

    VkRect2D scissor;
    scissor.offset.x = 0;
    scissor.offset.y = 0;
    scissor.extent.width = WINDOW_WIDTH;
    scissor.extent.height = WINDOW_HEIGHT;

    VkPipelineViewportStateCreateInfo vpCreateInfo = {};
    vpCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vpCreateInfo.viewportCount = 1;
    vpCreateInfo.pViewports = &vp;
    vpCreateInfo.scissorCount = 1;
    vpCreateInfo.pScissors = &scissor;

    VkPipelineRasterizationStateCreateInfo rastCreateInfo = {};
    rastCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rastCreateInfo.polygonMode = VK_POLYGON_MODE_FILL;
    rastCreateInfo.cullMode = VK_CULL_MODE_NONE;
    rastCreateInfo.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rastCreateInfo.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo pipelineMSCreateInfo = {};
    pipelineMSCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    pipelineMSCreateInfo.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState blendAttachState = {};
    blendAttachState.colorWriteMask = 0xf;

    VkPipelineColorBlendStateCreateInfo blendCreateInfo = {};
    blendCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    blendCreateInfo.logicOp = VK_LOGIC_OP_COPY;
    blendCreateInfo.attachmentCount = 1;
    blendCreateInfo.pAttachments = &blendAttachState;

    VkPipelineLayoutCreateInfo layoutInfo = {};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &m_descriptorSetLayout;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &m_pushConstantRange;

    VkResult res = vkCreatePipelineLayout(m_core.GetDevice(), &layoutInfo, NULL, &m_pipelineLayout);
    CHECK_VULKAN_ERROR("vkCreatePipelineLayout error %d\n", res);

    VkGraphicsPipelineCreateInfo pipelineInfo = {};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = ARRAY_SIZE_IN_ELEMENTS(shaderStageCreateInfo);
    pipelineInfo.pStages = &shaderStageCreateInfo[0];
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &pipelineIACreateInfo;
    pipelineInfo.pViewportState = &vpCreateInfo;
    pipelineInfo.pRasterizationState = &rastCreateInfo;
    pipelineInfo.pMultisampleState = &pipelineMSCreateInfo;
    pipelineInfo.pColorBlendState = &blendCreateInfo;
    pipelineInfo.layout = m_pipelineLayout;
    pipelineInfo.renderPass = m_renderPass;
    pipelineInfo.basePipelineIndex = -1;

    res = vkCreateGraphicsPipelines(m_core.GetDevice(), VK_NULL_HANDLE, 1, &pipelineInfo, NULL, &m_pipeline);
    CHECK_VULKAN_ERROR("vkCreateGraphicsPipelines error %d\n", res);

    printf("Graphics pipeline created\n");
}


void OgldevVulkanApp::Init()
{
#ifdef WIN32
    m_pWindowControl = new Win32Control(m_appName.c_str());
#else            
    m_pWindowControl = new XCBControl();
#endif    
    m_pWindowControl->Init(WINDOW_WIDTH, WINDOW_HEIGHT);

    m_core.Init(m_pWindowControl);

    vkGetDeviceQueue(m_core.GetDevice(), m_core.GetQueueFamily(), 0, &m_queue);

    CreateSwapChain();
    CreateCommandBuffer();
    CreateVertexBuffer();
    CreateIndexBuffer();
    CreateDescriptorSetLayout();
    CreatePushConstantRange();
    AllocateDynamicBufferTransferSpace();
    CreateUniformBuffers();
    CreateDescriptorPool();
    CreateDescriptorSets();
    CreateRenderPass();
    CreateFramebuffer();
    CreateShaders();
    CreatePipeline();
    CreateSemaphores();
}


void OgldevVulkanApp::Run()
{
    while (true) {
        RenderScene();
    }
}


int main(int argc, char** argv)
{
    OgldevVulkanApp app("Tutorial 50");

    app.Init();

    app.Run();

    return 0;
}

