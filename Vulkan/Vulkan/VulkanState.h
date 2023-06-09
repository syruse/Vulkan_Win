#pragma once

#define GLM_FORCE_RADIANS
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <array>
#include "VulkanCore.h"

struct VulkanState {
    /** Using double buffering and vsync locks rendering to an integer fraction of the vsync rate.
        anyway the driver may execute some final operations before completly releasing subsequant buffer
        to avoid wasting time we can use really idle buffer by triple buffering involvement
    */
    static constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 3;  /// triple buffering to maximize performance

    struct Model {
        alignas(16) glm::mat4 model;
        alignas(16) glm::mat4 MVP;
    };

    struct ViewProj {
        alignas(16) glm::mat4 viewProj;
        alignas(16) glm::mat4 viewProjInverse;
    };

    struct PushConstant {
        alignas(16) glm::vec2 windowSize;  // alighned as vec4 or 16bytes
        alignas(16) glm::vec3 lightPos;
        alignas(16) glm::vec3 cameraPos;
    };

    struct DepthBuffer {
        VkFormat depthFormat{VK_FORMAT_UNDEFINED};
        VkImage depthImage{nullptr};
        VkDeviceMemory depthImageMemory{nullptr};
        VkImageView depthImageView{nullptr};
    };

    struct ColorBuffer {
        VkFormat colorFormat = VK_FORMAT_UNDEFINED;
        std::array<VkImage, MAX_FRAMES_IN_FLIGHT> colorBufferImage{nullptr};
        std::array<VkDeviceMemory, MAX_FRAMES_IN_FLIGHT> colorBufferImageMemory{nullptr};
        std::array<VkImageView, MAX_FRAMES_IN_FLIGHT> colorBufferImageView{nullptr};
    };

    struct GPassBuffer {
        uint8_t size{2u};
        ColorBuffer normal{};
        ColorBuffer color{};
    };

    struct SwapChain {
        VkSwapchainKHR handle{nullptr};
        std::array<VkImage, MAX_FRAMES_IN_FLIGHT> images{};
        std::array<VkImageView, MAX_FRAMES_IN_FLIGHT> views{};
    };

    struct UBO {
        std::array<VkBuffer, MAX_FRAMES_IN_FLIGHT> buffers{};
        std::array<VkDeviceMemory, MAX_FRAMES_IN_FLIGHT> buffersMemory{};
    };

    struct DynamicUBO {
        std::array<VkBuffer, MAX_FRAMES_IN_FLIGHT> buffers{};
        std::array<VkDeviceMemory, MAX_FRAMES_IN_FLIGHT> buffersMemory{};
    };

    VulkanState(std::wstring_view appName, uint16_t width, uint16_t height);

    uint16_t _width{0u};
    uint16_t _height{0u};
    VulkanCore _core{nullptr};
    SwapChain _swapChain{};
    VkQueue _queue{nullptr};
    VkCommandPool _cmdBufPool{nullptr};
    std::array<VkCommandBuffer, MAX_FRAMES_IN_FLIGHT> _cmdBufs{};
    UBO _ubo{};
    DynamicUBO _dynamicUbo{};
    uint32_t _modelUniformAlignment{0u};
    DepthBuffer _depthBuffer{};
    ColorBuffer _colorBuffer{};
    GPassBuffer _gPassBuffer{};
    PushConstant _pushConstant{};
};
