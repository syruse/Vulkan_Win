#pragma once

#define GLM_FORCE_RADIANS
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <array>
#include "VulkanCore.h"

#ifdef _WIN32
#include "Win32Control.h"
/// already included 'windows.h' with own implementations of aligned_alloc...
#elif __linux__
#include <stdlib.h>  // aligned_alloc/free
#include <cstring>   // memcpy
#include "XCBControl.h"
#define _aligned_free free
#define _aligned_malloc aligned_alloc
#else
/// other OS
#endif

struct VulkanState {
    /** Using double buffering and vsync locks rendering to an integer fraction of the vsync rate.
        anyway the driver may execute some final operations before completly releasing subsequant buffer
        to avoid wasting time we can use really idle buffer by triple buffering involvement
    */
    /** triple buffering maximize performance but we can run into cpu stuttering if gpu is not able to process 3 frames in time
        5 is available on most platforms (WIN, Linux, Android) in this case we don't need gamble with VK_PRESENT_MODE_MAILBOX_KHR
        which evicts pending frame from the gpu presenting queue and doesn't force cpu to wait available one
        and the same time it provides swapImage index in random order but our logic expects incremental m_currentFrame 
        for available semaphores and fences it may cause unexpected issues
    */
    static constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 5;

    struct Model {
        alignas(16) glm::mat4 model;
        alignas(16) glm::mat4 MVP;
    };

    struct ViewProj {
        alignas(16) glm::mat4 viewProj;
        alignas(16) glm::mat4 viewProjInverse;
        alignas(16) glm::mat4 lightViewProj;
        alignas(16) glm::mat4 proj;
        alignas(16) glm::mat4 view;
        alignas(16) glm::mat4 footPrintViewProj;
    };

    struct PushConstant {
        alignas(16) glm::vec4 windowSize{1.0f};  // alighned as vec4 or 16bytes
        alignas(16) glm::vec3 lightPos{1.0f};
        alignas(16) glm::vec4 cameraPos{1.0f}; // w component is tesselation level
        alignas(16) glm::vec4 windDirElapsedTimeMS{0.0f}; // vec3 is velocity and w is elapsedTime
    };

    struct DepthBuffer {
        VkFormat depthFormat{VK_FORMAT_UNDEFINED};
        VkImage depthImage{nullptr};
        VkDeviceMemory depthImageMemory{nullptr};
        VkImageView depthImageView{nullptr};
    };

    struct ColorBuffer {
        VkFormat colorFormat{VK_FORMAT_UNDEFINED};
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

    VulkanState(std::string_view appName, uint16_t width, uint16_t height);

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
    DepthBuffer _depthTempBuffer{};
    DepthBuffer _shadowMapBuffer{};
    ColorBuffer _colorBuffer{};
    ColorBuffer _ssaoBuffer{};
    DepthBuffer _footprintBuffer{};
    std::array<ColorBuffer, 2u> _bloomBuffer{}; // we need two ping-pong hdr buffers (hdr-> blurred hdr -> more blurred hdr...)
    GPassBuffer _gPassBuffer{};
    PushConstant _pushConstant{};
};
