#pragma once

#include "Win32Control.h"
#include "Utils.h"
#include <memory>

class IControl;

class VulkanCore
{
public:
    explicit VulkanCore(std::unique_ptr<IControl>&& winController);

    ~VulkanCore();

    void init();

    const VkPhysicalDevice& getPhysDevice() const;

    const VkSurfaceFormatKHR& getSurfaceFormat() const;

    const VkSurfaceCapabilitiesKHR getSurfaceCaps() const;

    const VkSurfaceKHR& getSurface() const { return m_surface; }

    int getQueueFamily() const { return m_gfxQueueFamily; }

    VkInstance& getInstance() { return m_inst; }

    VkDevice& getDevice() { return m_device; }

private:
    void createInstance();
    VkSurfaceKHR createSurface(VkInstance& inst);
    void selectPhysicalDevice();
    void createLogicalDevice();

    std::unique_ptr<IControl> m_winController = nullptr;

    // Vulkan objects
    VkInstance m_inst = nullptr;
    VkSurfaceKHR m_surface = nullptr;
    Utils::VulkanPhysicalDevices m_physDevices {};
    VkDevice m_device = nullptr;

    // Internal stuff
    int m_gfxDevIndex = - 1;
    int m_gfxQueueFamily = -1;;
};

