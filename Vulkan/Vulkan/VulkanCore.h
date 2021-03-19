#pragma once

#include "Win32Control.h"
#include "Utils.h"

class VulkanCore
{

public:
    VulkanCore(const char* pAppName);
    ~VulkanCore() = default;

    void Init(Win32Control* pWindowControl);

    const VkPhysicalDevice& GetPhysDevice() const;

    const VkSurfaceFormatKHR& GetSurfaceFormat() const;

    const VkSurfaceCapabilitiesKHR GetSurfaceCaps() const;

    const VkSurfaceKHR& GetSurface() const { return m_surface; }

    int GetQueueFamily() const { return m_gfxQueueFamily; }

    VkInstance& GetInstance() { return m_inst; }

    VkDevice& GetDevice() { return m_device; }

private:
    void CreateInstance();
    void CreateSurface();
    void SelectPhysicalDevice();
    void CreateLogicalDevice();

    // Vulkan objects
    VkInstance m_inst;
    VkSurfaceKHR m_surface;
    Utils::VulkanPhysicalDevices m_physDevices;
    VkDevice m_device;

    // Internal stuff
    std::string m_appName;
    int m_gfxDevIndex;
    int m_gfxQueueFamily;

};

