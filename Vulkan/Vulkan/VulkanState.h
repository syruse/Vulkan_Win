#pragma once

#include "VulkanCore.h"
#include <array>

struct VulkanState
{

    /** Using double buffering and vsync locks rendering to an integer fraction of the vsync rate.
        anyway the driver may execute some final operations before completly releasing subsequant buffer
        to avoid wasting time we can use really idle buffer by triple buffering usung
    */
    static constexpr size_t MAX_FRAMES_IN_FLIGHT = 3; /// triple buffering to maximize performance

    struct DepthBuffer
    {
        VkFormat depthFormat{ VK_FORMAT_UNDEFINED };
        VkImage depthImage{ nullptr };
        VkDeviceMemory depthImageMemory{ nullptr };
        VkImageView depthImageView{ nullptr };
    };

    struct ColorBuffer
    {
        VkFormat colorFormat = VK_FORMAT_UNDEFINED;
        std::array<VkImage, MAX_FRAMES_IN_FLIGHT> colorBufferImage{ nullptr };
        std::array<VkDeviceMemory, MAX_FRAMES_IN_FLIGHT> colorBufferImageMemory{ nullptr };
        std::array<VkImageView, MAX_FRAMES_IN_FLIGHT> colorBufferImageView{ nullptr };
    };

    VulkanState(std::wstring_view appName, uint16_t width, uint16_t height);

    uint16_t _width{ 0u };
    uint16_t _height{ 0u };
    VulkanCore _core{ nullptr };
    VkSwapchainKHR _swapChainKHR{ nullptr };
    VkQueue _queue{ nullptr };
    VkCommandPool _cmdBufPool{ nullptr };
    std::array<VkCommandBuffer, MAX_FRAMES_IN_FLIGHT> _cmdBufs{};
    std::array<VkImage, MAX_FRAMES_IN_FLIGHT> _swapChainImages{};
    std::array<VkImageView, MAX_FRAMES_IN_FLIGHT> _swapChainViews{};
    DepthBuffer _depthBuffer{};
    ColorBuffer _colorBuffer{};
};

