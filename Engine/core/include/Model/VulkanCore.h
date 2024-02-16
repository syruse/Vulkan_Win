#pragma once

#include <memory>
#include "IControl.h"
#include "Utils.h"

class IControl;

class VulkanCore {
public:
    explicit VulkanCore(std::unique_ptr<IControl>&& winController);

    ~VulkanCore();

    void init();

    VkPhysicalDevice getPhysDevice() const;

    const VkSurfaceFormatKHR& getSurfaceFormat() const;

    VkPresentModeKHR getPresentMode() const;

    const VkSurfaceCapabilitiesKHR& getSurfaceCaps() const;

    VkSurfaceKHR getSurface() const {
        return m_surface;
    }

    int getQueueFamily() const {
        return m_gfxQueueFamily;
    }

    VkInstance getInstance() const {
        return m_inst;
    }

    VkDevice getDevice() const {
        return m_device;
    }

    const std::unique_ptr<IControl>& getWinController() const {
        return m_winController;
    }

private:
    void createInstance();
    VkSurfaceKHR createSurface(VkInstance& inst);
    void selectPhysicalDevice();
    void createLogicalDevice();

    std::unique_ptr<IControl> m_winController = nullptr;

    // Vulkan objects
    VkInstance m_inst = nullptr;
    VkSurfaceKHR m_surface = nullptr;
    Utils::VulkanPhysicalDevices m_physDevices{};
    VkDevice m_device = nullptr;

    // Internal stuff
    int m_gfxDevIndex = -1;
    int m_gfxQueueFamily = -1;
};
