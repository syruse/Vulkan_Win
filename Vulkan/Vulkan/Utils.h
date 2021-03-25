#pragma once

#include "vulkan/vulkan.h"
#include <iostream>
#include <vector>
#include <string>
#include <sstream>

#ifdef _WIN32
#include <windows.h>
#endif

namespace Utils {
    struct VulkanPhysicalDevices {
        std::vector<VkPhysicalDevice> m_devices;
        std::vector<VkPhysicalDeviceProperties> m_devProps;
        std::vector< std::vector<VkQueueFamilyProperties> > m_qFamilyProps;
        std::vector< std::vector<VkBool32> > m_qSupportsPresent;
        std::vector< std::vector<VkSurfaceFormatKHR> > m_surfaceFormats;
        std::vector<VkSurfaceCapabilitiesKHR> m_surfaceCaps;
        std::vector< std::vector<VkPresentModeKHR> > m_presentModes;
    };

    template<class... Args>
    void printLog(bool isCritical, Args... args)
    {
        std::stringstream output;
        /// before c++17 int dummy[sizeof...(Args)] = { (output << args, 0)... };
        (output << ... << args) << "\n";

        const std::string& msg = output.str();
        std::cout << msg;

        if (isCritical)
        {
#ifdef _WIN32
            MessageBoxA(NULL, msg.c_str(), NULL, 0);
#elif __linux__
            ///TO DO
#endif
            throw std::runtime_error(msg.c_str());
        }
    }

    void printInfoF(const char* pFileName, size_t line, const char* pFuncName, const char* msg, ...);
    void printErrorF(const char* pFileName, size_t line, const char* pFuncName, const char* msg, ...);

    void VulkanCheckValidationLayerSupport();

    void VulkanEnumExtProps(std::vector<VkExtensionProperties>& ExtProps);

    void VulkanGetPhysicalDevices(const VkInstance& inst, const VkSurfaceKHR& Surface, VulkanPhysicalDevices& PhysDevices);

    size_t VulkanFindMemoryType(const VkPhysicalDevice& physicalDevice, const VkMemoryRequirements& memRequirements, VkMemoryPropertyFlags properties);

    void Vulkan—reateBuffer(const VkDevice& device, const VkPhysicalDevice& physicalDevice, VkDeviceSize size, VkBufferUsageFlags usage,
        VkMemoryPropertyFlags properties, VkBuffer& buffer, VkDeviceMemory& bufferMemory);

    void Vulkan—opyBuffer(VkDevice device, VkQueue queue, VkCommandPool cmdBufPool, VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size);

    VkShaderModule VulkanCreateShaderModule(VkDevice& device, std::string_view fileName);

    void VulkanCreateImage(const VkDevice& device, const VkPhysicalDevice& physicalDevice, uint32_t width, uint32_t height, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage,
        VkMemoryPropertyFlags properties, VkImage& image, VkDeviceMemory& imageMemory);

    void VulkanCreateTextureImage(const VkDevice& device, const VkPhysicalDevice& physicalDevice, const VkQueue& queue, const VkCommandPool& cmdBufPool,
        std::string_view pTextureFileName, VkImage& textureImage, VkDeviceMemory& textureImageMemory);

    void VulkanTransitionImageLayout(VkDevice device, VkQueue queue, VkCommandPool cmdBufPool, VkImage image, VkFormat format,
        VkImageLayout oldLayout, VkImageLayout newLayout, VkImageAspectFlags aspectMask = VK_IMAGE_ASPECT_COLOR_BIT);

    VkResult VulkanCreateImageView(const VkDevice& device, VkImage image, VkFormat format, VkImageAspectFlags aspectMask, VkImageView& imageView);

    bool VulkanFindSupportedFormat(const VkPhysicalDevice& physicalDevice, const std::vector<VkFormat>& candidates,
        VkImageTiling tiling, VkFormatFeatureFlags features, VkFormat& ret_format);
}

#define ARRAY_SIZE_IN_ELEMENTS(a) (sizeof(a)/sizeof(a[0]))
#define INFO(msg, ...) Utils::printInfoF(__FILE__, __LINE__, __FUNCTION__,  msg, __VA_ARGS__)
#define ERROR(msg, ...) Utils::printErrorF(__FILE__, __LINE__, __FUNCTION__, msg, __VA_ARGS__)
#define CHECK_VULKAN_ERROR(msg, res)    \
    if (res != VK_SUCCESS) {            \
        ERROR(msg, res);         \
    }



