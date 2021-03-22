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

    void printInfoF(const char* pFileName, size_t line, const char* msg, ...);
    void printErrorF(const char* pFileName, size_t line, const char* msg, ...);
    void VulkanEnumExtProps(std::vector<VkExtensionProperties>& ExtProps);
    void VulkanGetPhysicalDevices(const VkInstance& inst, const VkSurfaceKHR& Surface, VulkanPhysicalDevices& PhysDevices);
    size_t findMemoryType(const VkPhysicalDevice& physicalDevice, const VkMemoryRequirements& memRequirements, VkMemoryPropertyFlags properties);
    void createBuffer(const VkDevice& device, const VkPhysicalDevice& physicalDevice, VkDeviceSize size, VkBufferUsageFlags usage,
        VkMemoryPropertyFlags properties, VkBuffer& buffer, VkDeviceMemory& bufferMemory);
    void copyBuffer(VkDevice device, VkQueue queue, VkCommandPool cmdBufPool, VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size);
}

#define ARRAY_SIZE_IN_ELEMENTS(a) (sizeof(a)/sizeof(a[0]))
#define INFO(msg, ...) Utils::printInfoF(__FILE__, __LINE__, msg, __VA_ARGS__)
#define ERROR(msg, ...) Utils::printErrorF(__FILE__, __LINE__, msg, __VA_ARGS__)
#define CHECK_VULKAN_ERROR(msg, res)    \
    if (res != VK_SUCCESS) {            \
        ERROR(msg, res);         \
        abort();                        \
    }



