#pragma once

#include <vulkan/vulkan.h>
#include <array>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

namespace Utils {

struct VulkanPhysicalDevices {
    std::vector<VkPhysicalDevice> m_devices;
    std::vector<VkPhysicalDeviceProperties> m_devProps;
    std::vector<std::vector<VkQueueFamilyProperties> > m_qFamilyProps;
    std::vector<std::vector<VkBool32> > m_qSupportsPresent;
    std::vector<std::vector<VkSurfaceFormatKHR> > m_surfaceFormats;
    std::vector<VkSurfaceCapabilitiesKHR> m_surfaceCaps;
    std::vector<std::vector<VkPresentModeKHR> > m_presentModes;
};

template <class... Args>
void printLog(bool isCritical, Args... args) {
    std::stringstream output;
    /// before c++17 int dummy[sizeof...(Args)] = { (output << args, 0)... };
    (output << ... << args) << "\n";

    const std::string& msg = output.str();
    std::cout << msg;

    if (isCritical) {
#ifdef _WIN32
        MessageBoxA(NULL, msg.c_str(), NULL, 0);
#elif __linux__
        /// TO DO
#endif
        throw std::runtime_error(msg.c_str());
    }
}

void printInfoF(const char* pFileName, size_t line, const char* pFuncName, const char* msg, ...);
void printErrorF(const char* pFileName, size_t line, const char* pFuncName, const char* msg, ...);

std::string formPath(std::string_view dir, std::string_view fileName);

void VulkanCheckValidationLayerSupport();

void VulkanEnumExtProps(std::vector<VkExtensionProperties>& ExtProps);

void VulkanGetPhysicalDevices(VkInstance inst, VkSurfaceKHR Surface, VulkanPhysicalDevices& PhysDevices);

size_t VulkanFindMemoryType(VkPhysicalDevice physicalDevice, const VkMemoryRequirements& memRequirements,
                            VkMemoryPropertyFlags properties);

void VulkanCreateBuffer(VkDevice device, VkPhysicalDevice physicalDevice, VkDeviceSize size, VkBufferUsageFlags usage,
                        VkMemoryPropertyFlags properties, VkBuffer& buffer, VkDeviceMemory& bufferMemory);

void VulkanCreateExternalBuffer(VkDevice device, VkPhysicalDevice physicalDevice, VkDeviceSize size, VkBufferUsageFlags usage,
                        VkMemoryPropertyFlags properties, VkBuffer& buffer, VkDeviceMemory& bufferMemory);

void VulkanCopyBuffer(VkDevice device, VkQueue queue, VkCommandPool cmdBufPool, VkBuffer srcBuffer, VkBuffer dstBuffer,
                      VkDeviceSize size);

void VulkanCopyBufferToImage(VkDevice device, VkQueue queue, VkCommandPool cmdBufPool, VkBuffer buffer, VkImage image,
                             uint32_t width, uint32_t height, uint32_t layersCount = 1U);

VkShaderModule VulkanCreateShaderModule(VkDevice device, std::string_view fileName);

VkResult VulkanCreateImage(VkDevice device, VkPhysicalDevice physicalDevice, uint32_t width, uint32_t height, VkFormat format,
                           VkImageTiling tiling, VkImageUsageFlags usage, VkMemoryPropertyFlags properties, VkImage& image,
                           VkDeviceMemory& imageMemory, uint32_t mipLevels = 1U, uint32_t arrayLayers = 1U);

void VulkanGenerateMipmaps(VkDevice device, VkQueue queue, VkCommandPool cmdBufPool, VkImage image, VkFormat imageFormat,
                           int16_t texWidth, int16_t texHeight, uint8_t mipLevels, uint8_t layersAmount = 1u);

void VulkanImageMemoryBarrier(VkCommandBuffer commandBuffer, VkImage image, VkFormat format, VkImageLayout oldLayout,
                              VkImageLayout newLayout, VkImageAspectFlags aspectMask, uint32_t mipLevels, uint32_t layersCount,
                              VkAccessFlags srcAccessMask, VkAccessFlags dstAccessMask, VkPipelineStageFlags sourceStage,
                              VkPipelineStageFlags destinationStage);

void VulkanTransitionImageLayout(VkDevice device, VkQueue queue, VkCommandPool cmdBufPool, VkImage image, VkFormat format,
                                 VkImageLayout oldLayout, VkImageLayout newLayout,
                                 VkImageAspectFlags aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, uint32_t mipLevels = 1U,
                                 uint32_t layersCount = 1U);

VkResult VulkanCreateImageView(VkDevice device, VkImage image, VkFormat format, VkImageAspectFlags aspectMask,
                               VkImageView& imageView, uint32_t mipLevels = 1U, VkImageViewType type = VK_IMAGE_VIEW_TYPE_2D,
                               uint32_t layersCount = 1U);

bool VulkanFindSupportedFormat(VkPhysicalDevice physicalDevice, const std::vector<VkFormat>& candidates, VkImageTiling tiling,
                               VkFormatFeatureFlags features, VkFormat& ret_format);

template <class T>
void createGeneralBuffer(VkDevice device, VkPhysicalDevice physicalDevice, VkCommandPool cmdBufPool, VkQueue queue,
                         const std::vector<uint32_t>& indices, const std::vector<T>& vertices, VkDeviceSize& verticesBufferOffset,
                         VkBuffer& generalBuffer, VkDeviceMemory& generalBufferMemory);
}  // namespace Utils

#define MAX(a, b) (((a) > (b)) ? (a) : (b))
#define ARRAY_SIZE_IN_ELEMENTS(a) (sizeof(a) / sizeof(a[0]))
#define INFO_PARAM false, "\nin file: ", __FILE__, " at line: ", __LINE__, " from function: ", __FUNCTION__, " \n "
#define ERROR_PARAM true, "\nin file: ", __FILE__, " at line: ", __LINE__, " from function: ", __FUNCTION__, " \n "
#ifdef _WIN32
#define INFO_FORMAT(msg, ...) Utils::printInfoF(__FILE__, __LINE__, __FUNCTION__, msg, __VA_ARGS__)
#define ERROR_FORMAT(msg, ...) Utils::printErrorF(__FILE__, __LINE__, __FUNCTION__, msg, __VA_ARGS__)
#else  //__linux__ if __VA_ARGS__  is empty then preceding comma will be deleted to avoid error compilation
#define INFO_FORMAT(msg, ...) Utils::printInfoF(__FILE__, __LINE__, __FUNCTION__, msg, ##__VA_ARGS__)
#define ERROR_FORMAT(msg, ...) Utils::printErrorF(__FILE__, __LINE__, __FUNCTION__, msg, ##__VA_ARGS__)
#endif
#define CHECK_VULKAN_ERROR(msg, res) \
    if (res != VK_SUCCESS) {         \
        ERROR_FORMAT(msg, res);      \
        std::exit(EXIT_FAILURE);     \
    }
