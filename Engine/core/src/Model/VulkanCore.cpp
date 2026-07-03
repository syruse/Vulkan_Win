#include "VulkanCore.h"
#include <cassert>
#include <cstring>
#include <cwchar>
#include <vector>
#include "Utils.h"

#if defined(USE_DLSS) && USE_DLSS
#include <sl_consts.h>
#include <sl_dlss.h>
#include <sl_version.h>

void __cdecl MySlLogCallback(sl::LogType type, const char* msg) {
    (void)type;

    if (!msg) {
        return;
    }

    // Streamline/NGX can emit this every frame; it is expected and very noisy.
    if (std::strstr(msg, "vkGetQueryPoolResults failed: NOT READY") != nullptr) {
        return;
    }

    const char* text = msg;
    if (msg[0] == '%' && msg[1] == 's') {
        text = msg + 2;
    }

    Utils::printLog(INFO_PARAM, "[Streamline] ", text);
}
#endif

using namespace Utils;

VKAPI_ATTR VkBool32 VKAPI_CALL MyDebugReportCallback(VkDebugReportFlagsEXT flags, VkDebugReportObjectTypeEXT objectType,
                                                     uint64_t object, size_t location, int32_t messageCode,
                                                     const char* pLayerPrefix, const char* pMessage, void* pUserData) {
    Utils::printLog(INFO_PARAM, pMessage);
    return VK_FALSE;
}

VulkanCore::VulkanCore(std::unique_ptr<IControl>&& winController) : m_winController(std::move(winController)) {
#if defined(USE_DLSS) && USE_DLSS
    // Initialize Streamline as early as possible to avoid API-before-slInit warnings.
    initDLSS();
#endif
}

VulkanCore::~VulkanCore() {
#if defined(_DEBUG)
    if (m_callback != VK_NULL_HANDLE) {
        auto func = reinterpret_cast<PFN_vkDestroyDebugReportCallbackEXT>(
            vkGetInstanceProcAddr(m_inst, "vkDestroyDebugReportCallbackEXT"));
        if (func != nullptr) {
            func(m_inst, m_callback, nullptr);
        }
    }
#endif

    vkDestroyDevice(m_device, nullptr);
    vkDestroySurfaceKHR(m_inst, m_surface, nullptr);
    vkDestroyInstance(m_inst, nullptr);

#if defined(USE_DLSS) && USE_DLSS
    if (m_slShutdownFn) {
        m_slShutdownFn();
    }
    if (m_slModule) {
        FreeLibrary(m_slModule);
    }
#endif
}

void VulkanCore::init() {
    assert(m_winController);

#if defined(USE_DLSS) && USE_DLSS
    if (!m_slModule) {
        // Fallback: try once more in case constructor-time init failed due to runtime environment.
        initDLSS();
    }

    if (m_slModule) {
        auto sl_vkGetInstanceProcAddr = (PFN_vkGetInstanceProcAddr)GetProcAddress(m_slModule, "vkGetInstanceProcAddr");
        if (!sl_vkGetInstanceProcAddr) {
            Utils::printLog(INFO_PARAM, "Failed to get vkGetInstanceProcAddr from sl.interposer.dll, fallback to volkInitialize");
            volkInitialize();
        } else {
            volkInitializeCustom(sl_vkGetInstanceProcAddr);
        }
    } else {
        Utils::printLog(INFO_PARAM, "Failed to load sl.interposer.dll, fallback to volkInitialize");
        volkInitialize();
    }
#else
    volkInitialize();
#endif

    m_winController->init();

    std::vector<VkExtensionProperties> ExtProps;
    VulkanEnumExtProps(ExtProps);

    VulkanCheckValidationLayerSupport();

    createInstance();

    m_surface = createSurface(m_inst);
    assert(m_surface);

    Utils::printLog(INFO_PARAM, "Surface created");

    VulkanGetPhysicalDevices(m_inst, m_surface, m_physDevices);
    selectPhysicalDevice();
    createLogicalDevice();

#if defined(USE_DLSS) && USE_DLSS
    if (m_slGetFeatureFunctionFn && !m_slDLSSSetOptionsFn) {
        void* dlssSetOptionsPtr = nullptr;
        auto result = m_slGetFeatureFunctionFn(sl::kFeatureDLSS, "slDLSSSetOptions", dlssSetOptionsPtr);
        if (result == sl::Result::eOk) {
            m_slDLSSSetOptionsFn = reinterpret_cast<PfnSlDLSSSetOptions>(dlssSetOptionsPtr);
        }
        if (!m_slDLSSSetOptionsFn) {
            Utils::printLog(INFO_PARAM, "Failed to resolve slDLSSSetOptions from Streamline feature function table");
        }
    }
#endif
}

VkSurfaceKHR VulkanCore::createSurface(VkInstance& inst) {
    assert(m_winController);
    return m_winController->createSurface(inst);
}

VulkanCore::VendorId VulkanCore::getVendorId() const {
    assert(m_gfxDevIndex >= 0);
    uint32_t vendorId = m_physDevices.m_devProps[m_gfxDevIndex].vendorID;
    switch (vendorId) {
        case NVIDIA:
            return VendorId::NVIDIA;
        case AMD:
            return VendorId::AMD;
        case INTEL:
            return VendorId::INTEL;
        default:
            return VendorId::UNKNOWN;
    }
}

VkPhysicalDevice VulkanCore::getPhysDevice() const {
    assert(m_gfxDevIndex >= 0);
    return m_physDevices.m_devices[m_gfxDevIndex];
}

const VkSurfaceFormatKHR& VulkanCore::getSurfaceFormat() const {
    assert(m_gfxDevIndex >= 0);
    return m_physDevices.m_surfaceFormats[m_gfxDevIndex][0];
}

VkPresentModeKHR VulkanCore::getPresentMode() const {
    assert(m_gfxDevIndex >= 0);
    /** checking presence of the most eficient one with vsync
    (it doesn't block app if the queue is full and none is rendered yet
     we simply replace that one in queue with just prepared newest swap image) */
    
    for (const auto& availablePresentMode : m_physDevices.m_presentModes[m_gfxDevIndex]) {
    
        if (availablePresentMode == VK_PRESENT_MODE_MAILBOX_KHR) {
            return availablePresentMode;
        }
    }
    
    // returning common one (with vsync but it may block app untill gpu releases swap image)
    // but very unlikely since we have 5 frames in swap chain
    return VK_PRESENT_MODE_FIFO_KHR;
}

const VkSurfaceCapabilitiesKHR& VulkanCore::getSurfaceCaps() const {
    assert(m_gfxDevIndex >= 0);

    // TODO refresh it only when needed
    /// Note: must be refreshed for example when resizing
    ///       otherwise programm will use cached previous surface size causing the crash
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
        getPhysDevice(), m_surface, const_cast<VkSurfaceCapabilitiesKHR*>(&(m_physDevices.m_surfaceCaps[m_gfxDevIndex])));

    return m_physDevices.m_surfaceCaps[m_gfxDevIndex];
}

void VulkanCore::selectPhysicalDevice() {
    bool foundGfxDevice = false;
    uint32_t* pQueueFamilyCount = nullptr;

    for (size_t i = 0; i < m_physDevices.m_devices.size(); ++i) {
        for (size_t j = 0; j < m_physDevices.m_qFamilyProps[i].size(); ++j) {
            VkQueueFamilyProperties& QFamilyProp = m_physDevices.m_qFamilyProps[i][j];

            INFO_FORMAT("Family %d Num queues: %d\n", j, QFamilyProp.queueCount);
            VkQueueFlags flags = QFamilyProp.queueFlags;
            INFO_FORMAT("GFX %s, Compute %s, Transfer %s, Sparse binding %s\n",
                        (flags & VK_QUEUE_GRAPHICS_BIT) ? "Yes" : "No", (flags & VK_QUEUE_COMPUTE_BIT) ? "Yes" : "No",
                        (flags & VK_QUEUE_TRANSFER_BIT) ? "Yes" : "No", (flags & VK_QUEUE_SPARSE_BINDING_BIT) ? "Yes" : "No");

            if ((flags & VK_QUEUE_GRAPHICS_BIT) && m_physDevices.m_qSupportsPresent[i][j] && QFamilyProp.queueCount > 0u) {
                m_gfxDevIndex = i;
                m_queues.at(Queue_family::GFX_QUEUE_FAMILY).familyIndex = j;
                INFO_FORMAT("Using GFX device %d and queue family %d\n", m_gfxDevIndex, m_queues.at(Queue_family::GFX_QUEUE_FAMILY).familyIndex);

                pQueueFamilyCount = &QFamilyProp.queueCount;

                if (m_physDevices.m_devProps[i].deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
                    INFO_FORMAT("DISCRETE GPU FOUND!\n");
                    foundGfxDevice = true;
                    break;
                }
            }
        }

        if (foundGfxDevice) {
            break;
        }
    }

    if (m_gfxDevIndex == -1) {
        Utils::printLog(INFO_PARAM, "No GFX device found!");
        assert(0);
    } else if (pQueueFamilyCount) {
        --(*pQueueFamilyCount);  // reduce queue count by one since we will use one queue for main thread
    }
#if defined(USE_FSR) && USE_FSR
    // look up FSR 3 required queues
    // 1. FSRPresent
    for (size_t j = 0; j < m_physDevices.m_qFamilyProps[m_gfxDevIndex].size(); ++j) {
        VkQueueFamilyProperties& QFamilyProp = m_physDevices.m_qFamilyProps[m_gfxDevIndex][j];

        INFO_FORMAT("Family %d Num queues: %d\n", j, QFamilyProp.queueCount);
        VkQueueFlags flags = QFamilyProp.queueFlags;

        if ((flags & VK_QUEUE_GRAPHICS_BIT) && (flags & VK_QUEUE_COMPUTE_BIT) &&
            m_physDevices.m_qSupportsPresent[m_gfxDevIndex][j] && QFamilyProp.queueCount > 0) {
            m_queues.at(Queue_family::FSR_PRESENT_QUEUE_FAMILY).familyIndex = j;
            --QFamilyProp.queueCount;
            INFO_FORMAT("FSRPresent uses queue family %d\n", m_queues.at(Queue_family::FSR_PRESENT_QUEUE_FAMILY).familyIndex);
        }
    }
    // 2. FSRImageAcquire
    for (size_t j = 0; j < m_physDevices.m_qFamilyProps[m_gfxDevIndex].size(); ++j) {
        VkQueueFamilyProperties& QFamilyProp = m_physDevices.m_qFamilyProps[m_gfxDevIndex][j];

        INFO_FORMAT("Family %d Num queues: %d\n", j, QFamilyProp.queueCount);
        VkQueueFlags flags = QFamilyProp.queueFlags;

        if (!(flags & VK_QUEUE_COMPUTE_BIT) && !(flags & VK_QUEUE_GRAPHICS_BIT) && !(flags & VK_QUEUE_TRANSFER_BIT) &&
            QFamilyProp.queueCount > 0) {
            m_queues.at(Queue_family::FSR_IMAGE_ACQUIRE_QUEUE_FAMILY).familyIndex = j;
            --QFamilyProp.queueCount;
            INFO_FORMAT("FSRImageAcquire uses queue family %d\n", m_queues.at(Queue_family::FSR_IMAGE_ACQUIRE_QUEUE_FAMILY).familyIndex);
        }
    }
    if (m_queues.at(Queue_family::FSR_IMAGE_ACQUIRE_QUEUE_FAMILY).familyIndex == -1) {
        // no image acquire queue was found, look for a more general queue
        for (size_t j = 0; j < m_physDevices.m_qFamilyProps[m_gfxDevIndex].size(); ++j) {
            VkQueueFamilyProperties& QFamilyProp = m_physDevices.m_qFamilyProps[m_gfxDevIndex][j];

            INFO_FORMAT("Family %d Num queues: %d\n", j, QFamilyProp.queueCount);
            VkQueueFlags flags = QFamilyProp.queueFlags;

            if (!(flags & VK_QUEUE_COMPUTE_BIT) && !(flags & VK_QUEUE_GRAPHICS_BIT) && QFamilyProp.queueCount > 0) {
                m_queues.at(Queue_family::FSR_IMAGE_ACQUIRE_QUEUE_FAMILY).familyIndex = j;
                --QFamilyProp.queueCount;
                INFO_FORMAT("FSRImageAcquire uses queue family %d\n", m_queues.at(Queue_family::FSR_IMAGE_ACQUIRE_QUEUE_FAMILY).familyIndex);
            }
        }
    }
    if (m_queues.at(Queue_family::FSR_IMAGE_ACQUIRE_QUEUE_FAMILY).familyIndex == -1) {
        // no image acquire queue was found, look for a more general queue
        for (size_t j = 0; j < m_physDevices.m_qFamilyProps[m_gfxDevIndex].size(); ++j) {
            VkQueueFamilyProperties& QFamilyProp = m_physDevices.m_qFamilyProps[m_gfxDevIndex][j];

            INFO_FORMAT("Family %d Num queues: %d\n", j, QFamilyProp.queueCount);
            VkQueueFlags flags = QFamilyProp.queueFlags;

            if (!(flags & VK_QUEUE_GRAPHICS_BIT) && QFamilyProp.queueCount > 0) {
                m_queues.at(Queue_family::FSR_IMAGE_ACQUIRE_QUEUE_FAMILY).familyIndex = j;
                --QFamilyProp.queueCount;
                INFO_FORMAT("FSRImageAcquire uses queue family %d\n", m_queues.at(Queue_family::FSR_IMAGE_ACQUIRE_QUEUE_FAMILY).familyIndex);
            }
        }
    }
    // 3. FSRAsyncCompute
    for (size_t j = 0; j < m_physDevices.m_qFamilyProps[m_gfxDevIndex].size(); ++j) {
        VkQueueFamilyProperties& QFamilyProp = m_physDevices.m_qFamilyProps[m_gfxDevIndex][j];

        INFO_FORMAT("Family %d Num queues: %d\n", j, QFamilyProp.queueCount);
        VkQueueFlags flags = QFamilyProp.queueFlags;

        if ((flags & VK_QUEUE_COMPUTE_BIT) && !(flags & VK_QUEUE_GRAPHICS_BIT) && QFamilyProp.queueCount > 0) {
            m_queues.at(Queue_family::FSR_ASYNC_COMPUTE_QUEUE_FAMILY).familyIndex = j;
            --QFamilyProp.queueCount;
            INFO_FORMAT("FSRAsyncCompute uses queue family %d\n", m_queues.at(Queue_family::FSR_ASYNC_COMPUTE_QUEUE_FAMILY).familyIndex);
        }
    }
    if (m_queues.at(Queue_family::FSR_ASYNC_COMPUTE_QUEUE_FAMILY).familyIndex == -1) {
        // no async compute was found, look for a more general queue
        for (size_t j = 0; j < m_physDevices.m_qFamilyProps[m_gfxDevIndex].size(); ++j) {
            VkQueueFamilyProperties& QFamilyProp = m_physDevices.m_qFamilyProps[m_gfxDevIndex][j];

            INFO_FORMAT("Family %d Num queues: %d\n", j, QFamilyProp.queueCount);
            VkQueueFlags flags = QFamilyProp.queueFlags;

            if ((flags & VK_QUEUE_COMPUTE_BIT) && QFamilyProp.queueCount > 0) {
                m_queues.at(Queue_family::FSR_ASYNC_COMPUTE_QUEUE_FAMILY).familyIndex = j;
                --QFamilyProp.queueCount;
                INFO_FORMAT("FSRAsyncCompute uses queue family %d\n", m_queues.at(Queue_family::FSR_ASYNC_COMPUTE_QUEUE_FAMILY).familyIndex);
            }
        }
    }
#endif
}

#if defined(USE_DLSS) && USE_DLSS
bool VulkanCore::loadStreamline() {
    m_slModule = LoadLibraryA("sl.interposer.dll");

    if (!m_slModule) {
        Utils::printLog(INFO_PARAM, "Failed to load sl.interposer.dll");
        return false;
    }

    m_slInitFn = (PfnSlInit)GetProcAddress(m_slModule, "slInit");
    m_slShutdownFn = (PfnSlShutdown)GetProcAddress(m_slModule, "slShutdown");
    m_slGetFeatureRequirementsFn = (PfnSlGetFeatureRequirements)GetProcAddress(m_slModule, "slGetFeatureRequirements");
    m_slIsFeatureLoadedFn = (PfnSlIsFeatureLoaded)GetProcAddress(m_slModule, "slIsFeatureLoaded");
    m_slSetTagFn = (PfnSlSetTag)GetProcAddress(m_slModule, "slSetTag");
    m_slSetTagForFrameFn = (PfnSlSetTagForFrame)GetProcAddress(m_slModule, "slSetTagForFrame");
    m_slSetConstantsFn = (PfnSlSetConstants)GetProcAddress(m_slModule, "slSetConstants");
    m_slGetNewFrameTokenFn = (PfnSlGetNewFrameToken)GetProcAddress(m_slModule, "slGetNewFrameToken");
    m_slEvaluateFeatureFn = (PfnSlEvaluateFeature)GetProcAddress(m_slModule, "slEvaluateFeature");
    m_slGetFeatureFunctionFn = (PfnSlGetFeatureFunction)GetProcAddress(m_slModule, "slGetFeatureFunction");
    if (!m_slInitFn || !m_slShutdownFn || !m_slGetFeatureRequirementsFn || !m_slIsFeatureLoadedFn || !m_slSetTagFn ||
        !m_slSetTagForFrameFn || !m_slSetConstantsFn || !m_slGetNewFrameTokenFn || !m_slEvaluateFeatureFn ||
        !m_slGetFeatureFunctionFn) {
        Utils::printLog(INFO_PARAM,
                        "Failed to get address of one or more required Streamline functions (slInit/slShutdown/slGetFeatureRequirements/slIsFeatureLoaded/slSetTag/slSetTagForFrame/slSetConstants/slGetNewFrameToken/slEvaluateFeature/slGetFeatureFunction)");
        return false;
    }

    return true;
}

sl::Result VulkanCore::slIsFeatureLoadedSafe(sl::Feature feature, bool& loaded) const {
    if (!m_slIsFeatureLoadedFn) {
        loaded = false;
        return sl::Result::eErrorMissingOrInvalidAPI;
    }
    return m_slIsFeatureLoadedFn(feature, loaded);
}

sl::Result VulkanCore::slSetTagSafe(const sl::ViewportHandle& viewport, const sl::ResourceTag* tags, uint32_t numTags,
                                    sl::CommandBuffer* cmdBuffer) const {
    if (!m_slSetTagFn) {
        return sl::Result::eErrorMissingOrInvalidAPI;
    }
    return m_slSetTagFn(viewport, tags, numTags, cmdBuffer);
}

sl::Result VulkanCore::slSetTagForFrameSafe(const sl::FrameToken& frame, const sl::ViewportHandle& viewport,
                                            const sl::ResourceTag* tags, uint32_t numTags, sl::CommandBuffer* cmdBuffer) const {
    if (!m_slSetTagForFrameFn) {
        return sl::Result::eErrorMissingOrInvalidAPI;
    }
    return m_slSetTagForFrameFn(frame, viewport, tags, numTags, cmdBuffer);
}

sl::Result VulkanCore::slSetConstantsSafe(const sl::Constants& values, const sl::FrameToken& frame,
                                          const sl::ViewportHandle& viewport) const {
    if (!m_slSetConstantsFn) {
        return sl::Result::eErrorMissingOrInvalidAPI;
    }
    return m_slSetConstantsFn(values, frame, viewport);
}

sl::Result VulkanCore::slGetNewFrameTokenSafe(sl::FrameToken*& token, const uint32_t* frameIndex) const {
    if (!m_slGetNewFrameTokenFn) {
        token = nullptr;
        return sl::Result::eErrorMissingOrInvalidAPI;
    }
    return m_slGetNewFrameTokenFn(token, frameIndex);
}

sl::Result VulkanCore::slEvaluateFeatureSafe(sl::Feature feature, const sl::FrameToken& frame,
                                            const sl::BaseStructure** inputs, uint32_t numInputs,
                                            sl::CommandBuffer* cmdBuffer) const {
    if (!m_slEvaluateFeatureFn) {
        return sl::Result::eErrorMissingOrInvalidAPI;
    }
    return m_slEvaluateFeatureFn(feature, frame, inputs, numInputs, cmdBuffer);
}

sl::Result VulkanCore::slDLSSSetOptionsSafe(const sl::ViewportHandle& viewport, const sl::DLSSOptions& options) const {
    if (!m_slDLSSSetOptionsFn) {
        return sl::Result::eErrorMissingOrInvalidAPI;
    }
    return m_slDLSSSetOptionsFn(viewport, options);
}

// Initialize DLSS before creating Vulkan instance, so that Streamline can be loaded and initialized properly
void VulkanCore::initDLSS() {
    if (m_slModule && m_slInitFn) {
        return;
    }

    if (!loadStreamline())
        return;

    sl::Feature features[] = {sl::kFeatureDLSS};
    sl::Preferences pref;
    pref.featuresToLoad = features;
    pref.numFeaturesToLoad = _countof(features);
    pref.applicationId = 0x00000001;
    pref.engine = sl::EngineType::eCustom;
    pref.engineVersion = "1.0";
    pref.renderAPI = sl::RenderAPI::eVulkan;

    // Plugins are deployed next to executable in this project.
    wchar_t exePath[MAX_PATH]{};
    if (GetModuleFileNameW(nullptr, exePath, MAX_PATH) > 0) {
        wchar_t* slash = wcsrchr(exePath, L'\\');
        if (!slash) {
            slash = wcsrchr(exePath, L'/');
        }
        if (slash) {
            *slash = L'\0';
        }
    } else {
        wcscpy_s(exePath, L".");
    }
    const wchar_t* slPluginPaths[] = {exePath};
    pref.pathsToPlugins = slPluginPaths;
    pref.numPathsToPlugins = _countof(slPluginPaths);
    pref.renderAPI = sl::RenderAPI::eVulkan;
    pref.showConsole = false;
    pref.flags = pref.flags | sl::PreferenceFlags::eUseFrameBasedResourceTagging;

#ifdef _DEBUG
    pref.logLevel = sl::LogLevel::eDefault;
    pref.logMessageCallback = MySlLogCallback;
#else
    pref.logLevel = sl::LogLevel::eOff;
#endif

    if (m_slInitFn(pref, sl::kSDKVersion) != sl::Result::eOk) {
        Utils::printLog(INFO_PARAM, "Failed to initialize DLSS");
    }
}
#endif

void VulkanCore::createInstance() {
    assert(m_winController);

    VkApplicationInfo appInfo = {};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = m_winController->getAppName().data();
    appInfo.engineVersion = 1;
    appInfo.apiVersion = VK_API_VERSION_1_3;

    // Create a dynamic vector for instance extensions
    std::vector<const char*> finalInstanceExtensions;

#if defined(_DEBUG)
    finalInstanceExtensions.push_back(VK_EXT_DEBUG_REPORT_EXTENSION_NAME);
#endif
    finalInstanceExtensions.push_back(VK_KHR_SURFACE_EXTENSION_NAME);
#if defined(_WIN32) && defined(USE_CUDA) && USE_CUDA
    finalInstanceExtensions.push_back(VK_KHR_EXTERNAL_MEMORY_CAPABILITIES_EXTENSION_NAME);
    finalInstanceExtensions.push_back(VK_KHR_EXTERNAL_SEMAPHORE_CAPABILITIES_EXTENSION_NAME);
#endif
    finalInstanceExtensions.push_back(m_winController->getVulkanWindowSurfaceExtension().data());

#if defined(USE_DLSS) && USE_DLSS
    // Inject Instance Extensions required by Streamline DLSS
    if (m_slGetFeatureRequirementsFn) {
        sl::FeatureRequirements dlssReqs{};
        sl::Result slRes = m_slGetFeatureRequirementsFn(sl::kFeatureDLSS, dlssReqs);

        if (slRes == sl::Result::eOk) {
            for (uint32_t i = 0; i < dlssReqs.vkNumInstanceExtensions; ++i) {
                const char* extName = dlssReqs.vkInstanceExtensions[i];
                // Avoid duplication
                auto it = std::find_if(finalInstanceExtensions.begin(), finalInstanceExtensions.end(),
                                       [extName](const char* ext) { return strcmp(ext, extName) == 0; });

                if (it == finalInstanceExtensions.end()) {
                    finalInstanceExtensions.push_back(extName);
                }
            }
            Utils::printLog(INFO_PARAM, "Streamline DLSS instance extensions successfully injected");
        }
    }
#endif

#if defined(_DEBUG)
    const char* pInstLayers[] = {"VK_LAYER_KHRONOS_validation"};
#endif

    VkInstanceCreateInfo instInfo = {};
    instInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instInfo.pApplicationInfo = &appInfo;
#if defined(_DEBUG)
    instInfo.enabledLayerCount = ARRAY_SIZE_IN_ELEMENTS(pInstLayers);
    instInfo.ppEnabledLayerNames = pInstLayers;
#endif

    // Pass the dynamic extensions vector
    instInfo.enabledExtensionCount = static_cast<uint32_t>(finalInstanceExtensions.size());
    instInfo.ppEnabledExtensionNames = finalInstanceExtensions.data();

    VkResult res = vkCreateInstance(&instInfo, nullptr, &m_inst);
    CHECK_VULKAN_ERROR("vkCreateInstance %d\n", res);

    // Load instance-level function pointers (including VK_KHR_win32_surface commands).
    volkLoadInstance(m_inst);

#if defined(_DEBUG)
    // Get the address to the vkCreateDebugReportCallbackEXT function
    PFN_vkCreateDebugReportCallbackEXT my_vkCreateDebugReportCallbackEXT =
        reinterpret_cast<PFN_vkCreateDebugReportCallbackEXT>(vkGetInstanceProcAddr(m_inst, "vkCreateDebugReportCallbackEXT"));

    // Register the debug callback
    VkDebugReportCallbackCreateInfoEXT callbackCreateInfo{};
    callbackCreateInfo.sType = VK_STRUCTURE_TYPE_DEBUG_REPORT_CREATE_INFO_EXT;
    callbackCreateInfo.flags =
        VK_DEBUG_REPORT_ERROR_BIT_EXT | VK_DEBUG_REPORT_WARNING_BIT_EXT | VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT;
    callbackCreateInfo.pfnCallback = &MyDebugReportCallback;

    res = my_vkCreateDebugReportCallbackEXT(m_inst, &callbackCreateInfo, nullptr, &m_callback);
    CHECK_VULKAN_ERROR("my_vkCreateDebugReportCallbackEXT error %d\n", res);
#endif
}

void VulkanCore::createLogicalDevice() {
    using familyIndexType = int;
    std::map<familyIndexType, std::vector<Queue_family>> queueFamilies;

    for (auto& queue : m_queues) {
        if (queue.second.familyIndex > -1) {
            queueFamilies[queue.second.familyIndex].push_back(queue.first);
            queue.second.queueIndex = queueFamilies[queue.second.familyIndex].size() - 1;
        }
    }

    std::vector<float> queuePriorities(m_queues.size(), 1.0f);  // all queues have the same priority
    std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
    for (const auto& [key, value] : queueFamilies) {
        VkDeviceQueueCreateInfo qInfo = {};
        qInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        qInfo.queueFamilyIndex = key;
        qInfo.queueCount = static_cast<uint32_t>(value.size());
        qInfo.pQueuePriorities = queuePriorities.data();
        queueCreateInfos.push_back(qInfo);
    }

    VkDeviceCreateInfo devInfo = {};
    std::vector<const char*> finalExtensions;

    // Base extensions
    finalExtensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);

    // Declare feature structures for Vulkan 1.2 and 1.3
    VkPhysicalDeviceVulkan12Features deviceFeatures12{};
    deviceFeatures12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;

    VkPhysicalDeviceVulkan13Features deviceFeatures13{};
    deviceFeatures13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;

#if defined(_WIN32) && defined(USE_CUDA) && USE_CUDA
    finalExtensions.push_back(VK_KHR_EXTERNAL_MEMORY_WIN32_EXTENSION_NAME);
    finalExtensions.push_back(VK_KHR_EXTERNAL_SEMAPHORE_WIN32_EXTENSION_NAME);
    finalExtensions.push_back(VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME);

    // Set default features required for CUDA interop
    deviceFeatures12.timelineSemaphore = VK_TRUE;
    deviceFeatures12.separateDepthStencilLayouts = VK_TRUE;  // for DEPTH_ATTACHMENT_OPTIMAL
#endif

#if defined(USE_DLSS) && USE_DLSS
    if (m_slGetFeatureRequirementsFn) {
        sl::FeatureRequirements dlssReqs{};
        sl::Result slRes = m_slGetFeatureRequirementsFn(sl::kFeatureDLSS, dlssReqs);

        if (slRes == sl::Result::eOk) {
            // Add DLSS device extensions to the finalExtensions vector
            for (uint32_t i = 0; i < dlssReqs.vkNumDeviceExtensions; ++i) {
                // Add only if not already present
                const char* extName = dlssReqs.vkDeviceExtensions[i];
                auto it = std::find_if(finalExtensions.begin(), finalExtensions.end(),
                                       [extName](const char* ext) { return strcmp(ext, extName) == 0; });

                if (it == finalExtensions.end()) {
                    finalExtensions.push_back(extName);
                }
            }
            Utils::printLog(INFO_PARAM, "Streamline DLSS device extensions successfully injected");

            // Unpack physical device features required by Streamline DLSS
            VkPhysicalDeviceVulkan12Features dlssFeatures12 =
                sl::getVkPhysicalDeviceVulkan12Features(dlssReqs.vkNumFeatures12, dlssReqs.vkFeatures12);
            VkPhysicalDeviceVulkan13Features dlssFeatures13 =
                sl::getVkPhysicalDeviceVulkan13Features(dlssReqs.vkNumFeatures13, dlssReqs.vkFeatures13);

            // Merge Vulkan 1.2 features using logical OR to preserve existing options
            deviceFeatures12.samplerMirrorClampToEdge |= dlssFeatures12.samplerMirrorClampToEdge;
            deviceFeatures12.drawIndirectCount |= dlssFeatures12.drawIndirectCount;
            deviceFeatures12.storageBuffer8BitAccess |= dlssFeatures12.storageBuffer8BitAccess;
            deviceFeatures12.uniformAndStorageBuffer8BitAccess |= dlssFeatures12.uniformAndStorageBuffer8BitAccess;
            deviceFeatures12.storagePushConstant8 |= dlssFeatures12.storagePushConstant8;
            deviceFeatures12.shaderBufferInt64Atomics |= dlssFeatures12.shaderBufferInt64Atomics;
            deviceFeatures12.shaderSharedInt64Atomics |= dlssFeatures12.shaderSharedInt64Atomics;
            deviceFeatures12.shaderFloat16 |= dlssFeatures12.shaderFloat16;
            deviceFeatures12.shaderInt8 |= dlssFeatures12.shaderInt8;
            deviceFeatures12.descriptorIndexing |= dlssFeatures12.descriptorIndexing;
            deviceFeatures12.shaderUniformBufferArrayNonUniformIndexing |=
                dlssFeatures12.shaderUniformBufferArrayNonUniformIndexing;
            deviceFeatures12.shaderSampledImageArrayNonUniformIndexing |=
                dlssFeatures12.shaderSampledImageArrayNonUniformIndexing;

            // Merge Vulkan 1.3 features
            deviceFeatures13.dynamicRendering |= dlssFeatures13.dynamicRendering;
            deviceFeatures13.synchronization2 |= dlssFeatures13.synchronization2;
            deviceFeatures13.maintenance4 |= dlssFeatures13.maintenance4;

            Utils::printLog(INFO_PARAM, "Streamline DLSS hardware features successfully merged");
        } else {
            Utils::printLog(INFO_PARAM, "Warning: DLSS is not supported on this hardware (slGetFeatureRequirements failed)");
        }
    }
#endif

    // Build the pNext chain for device creation
    deviceFeatures13.pNext = &deviceFeatures12;
    devInfo.pNext = &deviceFeatures13;

    VkPhysicalDeviceFeatures deviceFeatures{};
    deviceFeatures.samplerAnisotropy = VK_TRUE;
    deviceFeatures.tessellationShader = VK_TRUE;
    deviceFeatures.depthClamp = VK_TRUE;        // for pRasterizationState->depthClampEnable
    deviceFeatures.dualSrcBlend = VK_TRUE;      // for VK_BLEND_FACTOR_SRC1_ALPHA
    deviceFeatures.independentBlend = VK_TRUE;  // allow different blend state for motion-vector attachment

    devInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;

    devInfo.enabledExtensionCount = static_cast<uint32_t>(finalExtensions.size());
    devInfo.ppEnabledExtensionNames = finalExtensions.data();

    devInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
    devInfo.pQueueCreateInfos = queueCreateInfos.data();
    devInfo.pEnabledFeatures = &deviceFeatures;

    VkResult res = vkCreateDevice(getPhysDevice(), &devInfo, nullptr, &m_device);

    CHECK_VULKAN_ERROR("vkCreateDevice error %d\n", res);

    // Load device-level function pointers for extension/device commands.
    volkLoadDevice(m_device);

    Utils::printLog(INFO_PARAM, "Device created");

    for (auto& queue : m_queues) {
        if (queue.second.familyIndex > -1) {
            vkGetDeviceQueue(m_device, queue.second.familyIndex, queue.second.queueIndex, &queue.second.queue);
        }
    }

}