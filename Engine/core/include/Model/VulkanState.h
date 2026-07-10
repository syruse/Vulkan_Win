#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <array>
#include <vector>
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
        Also 5 introduce some latency if it's not VK_PRESENT_MODE_MAILBOX_KHR
    */
    static constexpr uint32_t REQUESTED_FRAMES_IN_FLIGHT = 3;

    static constexpr VkShaderStageFlags PUSH_CONSTANT_STAGE_FLAGS =
        VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT;

    struct Model {
        alignas(16) glm::mat4 model;
        alignas(16) glm::mat4 MVP;
        alignas(16) glm::mat4 prevModel;
        // 192 bytes + 64 bytes padding = 256 bytes (power of 2 for UBO alignment)
        alignas(16) glm::mat4 padding; 
    };

    struct ViewProj {
        alignas(16) glm::mat4 viewProj;
        alignas(16) glm::mat4 viewProjInverse;
        alignas(16) glm::mat4 lightViewProj;
        alignas(16) glm::mat4 proj;
        alignas(16) glm::mat4 view;
        alignas(16) glm::mat4 footPrintViewProj;
        alignas(16) glm::mat4 prevViewProj;
    };

    struct PushConstant {
        alignas(16) glm::vec4 windowSize{1.0f};  // alighned as vec4 or 16bytes
        alignas(16) glm::vec4 lightPos{1.0f};  //w is elapsedMS for previous frame
        alignas(16) glm::vec4 cameraPos{1.0f}; // w component is tesselation level
        alignas(16) glm::vec4 windDirElapsedTimeMS{0.0f}; // vec3 is velocity and w is elapsedTime
    };

    struct DepthBuffer {
        VkFormat depthFormat{VK_FORMAT_UNDEFINED};
        VkImage depthImage{nullptr};
        VkDeviceMemory depthImageMemory{nullptr};
        VkImageView depthImageView{nullptr};
        uint16_t& width;
        uint16_t& height;

        DepthBuffer(uint16_t& width, uint16_t& height) : width(width), height(height) {
        }
    };

    struct ColorBuffer {
        VkFormat colorFormat{VK_FORMAT_UNDEFINED};
        std::vector<VkImage> colorBufferImage{};
        std::vector<VkDeviceMemory> colorBufferImageMemory{};
        std::vector<VkImageView> colorBufferImageView{};
    };

    struct GPassBuffer {
        uint8_t size{2u};
        ColorBuffer normal{};
        ColorBuffer color{};
    };

    struct SwapChain {
        VkSwapchainKHR handle{nullptr};
        std::vector<VkImage> images{};
        std::vector<VkImageView> views{};
    };

    struct UBO {
        std::vector<VkBuffer> buffers{};
        std::vector<VkDeviceMemory> buffersMemory{};
    };

    struct DynamicUBO {
        std::vector<VkBuffer> buffers{};
        std::vector<VkDeviceMemory> buffersMemory{};
    };

    VulkanState(std::string_view appName, uint16_t windowWidth, uint16_t windowHeight, uint16_t offscreenWidth = 0u, uint16_t offscreenHeight = 0u);

    uint16_t _windowWidth{0u};
    uint16_t _windowHeight{0u};
    // offscreen render targets for shadow map, ssao, footprint and main render pass
    uint16_t _offscreenWidth{0u};
    uint16_t _offscreenHeight{0u};
    uint16_t _footPrintWidthAndHeight{8000u};
    uint16_t _shadowMapWidthAndHeight{8000u};
    VulkanCore _core{nullptr};
    uint32_t _swapchainImageCount{0u};
    SwapChain _swapChain{};
    VkQueue _queue{nullptr};
    VkCommandPool _cmdBufPool{nullptr};
    std::vector<VkCommandBuffer> _cmdBufs{};
    UBO _ubo{};
    DynamicUBO _dynamicUbo{};
    uint32_t _modelUniformAlignment{0u};
    DepthBuffer _depthBuffer{_offscreenWidth, _offscreenHeight};
    DepthBuffer _depthTempBuffer{_offscreenWidth, _offscreenHeight};
    DepthBuffer _shadowMapBuffer{_shadowMapWidthAndHeight, _shadowMapWidthAndHeight};
    ColorBuffer _colorBuffer{};
    ColorBuffer _viewSpaceBuffer{}; // this is for ssao generation
    ColorBuffer _motionVectorsBuffer{};  // this is for DLAA\TAA
    ColorBuffer _ssaoBuffer{};
    ColorBuffer _shadingBuffer{}; // this final color buffer devoted to shading only (blurring applying for SSAO and blend with current color)
    ColorBuffer _dlssOutputBuffer{}; // it's needed for applying the UI on top of the DLSS output
    DepthBuffer _footprintBuffer{_footPrintWidthAndHeight, _footPrintWidthAndHeight};
    std::array<ColorBuffer, 2u> _bloomBuffer{}; // we need two ping-pong hdr buffers (hdr-> blurred hdr -> more blurred hdr...)
    GPassBuffer _gPassBuffer{};
    PushConstant _pushConstant{};
};
