#pragma once

// you can activate debug mode by defining VK_DEBUG_ENABLED in CMAKELIST
// #define VK_DEBUG_ENABLED

#include <map>
#include <memory>
#include "IControl.h"
#include "Utils.h"

class IControl;

class VulkanCore {
public:
    enum Queue_family {
        GFX_QUEUE_FAMILY = 0,
#if defined(USE_FSR) && USE_FSR
        FSR_PRESENT_QUEUE_FAMILY,
        FSR_IMAGE_ACQUIRE_QUEUE_FAMILY,
        FSR_ASYNC_COMPUTE_QUEUE_FAMILY
#endif
    };

    struct Queue {
        int familyIndex = -1;  // index in m_physDevices.m_qFamilyProps[m_gfxDevIndex]
        int queueIndex = -1;   // index in m_physDevices.m_qFamilyProps[m_gfxDevIndex][familyIndex].queueCount
        VkQueue queue = nullptr;
    };

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
        return m_queues.at(Queue_family::GFX_QUEUE_FAMILY).familyIndex;
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

    const std::map<Queue_family, Queue>& getAllQueues() const {
        return m_queues;
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

    // gpu adapter index
    int m_gfxDevIndex = -1;

    std::map<Queue_family, Queue> m_queues{{GFX_QUEUE_FAMILY, {-1, 0, nullptr}},
#if defined(USE_FSR) && USE_FSR
                                           {FSR_PRESENT_QUEUE_FAMILY, {-1, 0, nullptr}},
                                           {FSR_IMAGE_ACQUIRE_QUEUE_FAMILY, {-1, 0, nullptr}},
                                           {FSR_ASYNC_COMPUTE_QUEUE_FAMILY, {-1, 0, nullptr}},
#endif    
    };
};
