#pragma once

#include <map>
#include <memory>
#include "IControl.h"
#include "Utils.h"

#if defined(USE_DLSS) && USE_DLSS
#include "sl_helpers_vk.h" 
#include <sl.h>
#include <sl_dlss.h>
// signuture of slInit from sl.h
typedef sl::Result (*PfnSlInit)(const sl::Preferences& pref, uint64_t version);
typedef sl::Result (*PfnSlShutdown)();
typedef sl::Result (*PfnSlGetFeatureRequirements)(sl::Feature feature, sl::FeatureRequirements& requirements);
typedef sl::Result (*PfnSlIsFeatureLoaded)(sl::Feature feature, bool& loaded);
typedef sl::Result (*PfnSlSetTag)(const sl::ViewportHandle& viewport, const sl::ResourceTag* tags, uint32_t numTags,
                                  sl::CommandBuffer* cmdBuffer);
typedef sl::Result (*PfnSlSetTagForFrame)(const sl::FrameToken& frame, const sl::ViewportHandle& viewport,
                                          const sl::ResourceTag* tags, uint32_t numTags, sl::CommandBuffer* cmdBuffer);
typedef sl::Result (*PfnSlSetConstants)(const sl::Constants& values, const sl::FrameToken& frame,
                                        const sl::ViewportHandle& viewport);
typedef sl::Result (*PfnSlGetNewFrameToken)(sl::FrameToken*& token, const uint32_t* frameIndex);
typedef sl::Result (*PfnSlEvaluateFeature)(sl::Feature feature, const sl::FrameToken& frame,
                                           const sl::BaseStructure** inputs, uint32_t numInputs,
                                           sl::CommandBuffer* cmdBuffer);
typedef sl::Result (*PfnSlGetFeatureFunction)(sl::Feature feature, const char* functionName, void*& function);
typedef sl::Result (*PfnSlDLSSSetOptions)(const sl::ViewportHandle& viewport, const sl::DLSSOptions& options);
typedef sl::Result (*PfnSlIsFeatureSupported)(sl::Feature feature, const sl::AdapterInfo& adapterInfo);
typedef sl::Result (*PfnSlSetFeatureLoaded)(sl::Feature feature, bool value);
#endif 

class IControl;

class VulkanCore {
public:
    enum VendorId : uint32_t { 
        NVIDIA = 0x10DE, 
        AMD = 0x1002, 
        INTEL = 0x8086, 
        UNKNOWN = 0xFFFF 
    };

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

    VendorId getVendorId() const;

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

#if defined(USE_DLSS) && USE_DLSS
    sl::Result slIsFeatureLoadedSafe(sl::Feature feature, bool& loaded) const;
    sl::Result slSetTagSafe(const sl::ViewportHandle& viewport, const sl::ResourceTag* tags, uint32_t numTags,
                            sl::CommandBuffer* cmdBuffer) const;
    sl::Result slSetTagForFrameSafe(const sl::FrameToken& frame, const sl::ViewportHandle& viewport,
                                    const sl::ResourceTag* tags, uint32_t numTags, sl::CommandBuffer* cmdBuffer) const;
    sl::Result slSetConstantsSafe(const sl::Constants& values, const sl::FrameToken& frame,
                                  const sl::ViewportHandle& viewport) const;
    sl::Result slGetNewFrameTokenSafe(sl::FrameToken*& token, const uint32_t* frameIndex) const;
    sl::Result slEvaluateFeatureSafe(sl::Feature feature, const sl::FrameToken& frame,
                                     const sl::BaseStructure** inputs, uint32_t numInputs,
                                     sl::CommandBuffer* cmdBuffer) const;
    sl::Result slDLSSSetOptionsSafe(const sl::ViewportHandle& viewport, const sl::DLSSOptions& options) const;
#endif

    bool isDlssSupported() const {
        return m_isDlssSupported;
    }

private:
    void createInstance();
#if defined(USE_DLSS) && USE_DLSS
    bool loadStreamline();
    void initDLSS();
#endif
    VkSurfaceKHR createSurface(VkInstance& inst);
    void selectPhysicalDevice();
    void createLogicalDevice();

    std::unique_ptr<IControl> m_winController = nullptr;

    // Vulkan objects
    VkInstance m_inst = nullptr;
    VkSurfaceKHR m_surface = nullptr;
    Utils::VulkanPhysicalDevices m_physDevices{};
    VkDevice m_device = nullptr;
    bool m_isDlssSupported = false;
#if defined(_DEBUG)
    VkDebugReportCallbackEXT m_callback = nullptr;
#endif
    // gpu adapter index
    int m_gfxDevIndex = -1;

    std::map<Queue_family, Queue> m_queues{{GFX_QUEUE_FAMILY, {-1, 0, nullptr}},
#if defined(USE_FSR) && USE_FSR
                                           {FSR_PRESENT_QUEUE_FAMILY, {-1, 0, nullptr}},
                                           {FSR_IMAGE_ACQUIRE_QUEUE_FAMILY, {-1, 0, nullptr}},
                                           {FSR_ASYNC_COMPUTE_QUEUE_FAMILY, {-1, 0, nullptr}},
#endif    
    };
#if defined(USE_DLSS) && USE_DLSS
    HMODULE m_slModule = nullptr;
    PfnSlInit m_slInitFn = nullptr;
    PfnSlShutdown m_slShutdownFn = nullptr;
    PfnSlGetFeatureRequirements m_slGetFeatureRequirementsFn = nullptr;
    PfnSlIsFeatureLoaded m_slIsFeatureLoadedFn = nullptr;
    PfnSlSetTag m_slSetTagFn = nullptr;
    PfnSlSetTagForFrame m_slSetTagForFrameFn = nullptr;
    PfnSlSetConstants m_slSetConstantsFn = nullptr;
    PfnSlGetNewFrameToken m_slGetNewFrameTokenFn = nullptr;
    PfnSlEvaluateFeature m_slEvaluateFeatureFn = nullptr;
    PfnSlGetFeatureFunction m_slGetFeatureFunctionFn = nullptr;
    PfnSlDLSSSetOptions m_slDLSSSetOptionsFn = nullptr;
    PfnSlIsFeatureSupported m_slIsFeatureSupportedFn = nullptr;
    PfnSlSetFeatureLoaded m_slSetFeatureLoadedFn = nullptr;
#endif
};
