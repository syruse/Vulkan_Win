#pragma once

#include "vulkan/vulkan.h"
#include <vector>

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

    void printError(const char* pFileName, size_t line, const char* msg, ...);
    void VulkanEnumExtProps(std::vector<VkExtensionProperties>& ExtProps);
    void VulkanGetPhysicalDevices(const VkInstance& inst, const VkSurfaceKHR& Surface, VulkanPhysicalDevices& PhysDevices);

}

#define ARRAY_SIZE_IN_ELEMENTS(a) (sizeof(a)/sizeof(a[0]))
#define ERROR(msg, ...) Utils::printError(__FILE__, __LINE__, msg, __VA_ARGS__)
#define CHECK_VULKAN_ERROR(msg, res)    \
    if (res != VK_SUCCESS) {            \
        ERROR(msg, res);         \
        abort();                        \
    }



