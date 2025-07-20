#include "VulkanCore.h"
#include <cassert>
#include <vector>
#include "Utils.h"

#if defined(_WIN32) && defined(USE_CUDA) && USE_CUDA
#include <vulkan/vulkan_win32.h>
#endif

using namespace Utils;

VKAPI_ATTR VkBool32 VKAPI_CALL MyDebugReportCallback(VkDebugReportFlagsEXT flags, VkDebugReportObjectTypeEXT objectType,
                                                     uint64_t object, size_t location, int32_t messageCode,
                                                     const char* pLayerPrefix, const char* pMessage, void* pUserData) {
    Utils::printLog(INFO_PARAM, pMessage);
    return VK_FALSE;
}

VulkanCore::VulkanCore(std::unique_ptr<IControl>&& winController) : m_winController(std::move(winController)) {
}

VulkanCore::~VulkanCore() {
#if defined(_DEBUG) && defined(VK_DEBUG_ENABLED)
    // Get the address to the vkCreateDebugReportCallbackEXT function
    auto func =
        reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(m_inst, "vkDestroyDebugUtilsMessengerEXT"));
    if (func != nullptr) {
        func(m_inst, nullptr, nullptr);
    }
#endif

    vkDestroyDevice(m_device, nullptr);
    vkDestroySurfaceKHR(m_inst, m_surface, nullptr);
    vkDestroyInstance(m_inst, nullptr);
}

void VulkanCore::init() {
    assert(m_winController);

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
}

VkSurfaceKHR VulkanCore::createSurface(VkInstance& inst) {
    assert(m_winController);
    return m_winController->createSurface(inst);
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
    /** it may expose us to some risks since it provides image indeces in random order
    *   see comment for MAX_FRAMES_IN_FLIGHT
    * 
    *   for (const auto& availablePresentMode : m_physDevices.m_presentModes[m_gfxDevIndex]) {
    *   
    *       if (availablePresentMode == VK_PRESENT_MODE_MAILBOX_KHR) {
    *           return availablePresentMode;
    *       }
    *   }
    */
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
    for (size_t i = 0; i < m_physDevices.m_devices.size(); ++i) {
        for (size_t j = 0; j < m_physDevices.m_qFamilyProps[i].size(); ++j) {
            VkQueueFamilyProperties& QFamilyProp = m_physDevices.m_qFamilyProps[i][j];

            INFO_FORMAT("Family %d Num queues: %d\n", j, QFamilyProp.queueCount);
            VkQueueFlags flags = QFamilyProp.queueFlags;
            INFO_FORMAT("GFX %s, Compute %s, Transfer %s, Sparse binding %s\n",
                        (flags & VK_QUEUE_GRAPHICS_BIT) ? "Yes" : "No", (flags & VK_QUEUE_COMPUTE_BIT) ? "Yes" : "No",
                        (flags & VK_QUEUE_TRANSFER_BIT) ? "Yes" : "No", (flags & VK_QUEUE_SPARSE_BINDING_BIT) ? "Yes" : "No");

            if ((flags & VK_QUEUE_GRAPHICS_BIT)) {
                if (!m_physDevices.m_qSupportsPresent[i][j]) {
                    Utils::printLog(INFO_PARAM, "Present is not supported");
                    continue;
                }

                m_gfxDevIndex = i;
                m_gfxQueueFamily = j;
                INFO_FORMAT("Using GFX device %d and queue family %d\n", m_gfxDevIndex, m_gfxQueueFamily);

                if (m_physDevices.m_devProps[i].deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
                    INFO_FORMAT("DISCRETE GPU FOUND!\n");
                    return;
                }
            }
        }
    }

    if (m_gfxDevIndex == -1) {
        Utils::printLog(INFO_PARAM, "No GFX device found!");
        assert(0);
    }
}

void VulkanCore::createInstance() {
    assert(m_winController);

    VkApplicationInfo appInfo = {};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = m_winController->getAppName().data();
    appInfo.engineVersion = 1;
    appInfo.apiVersion = VK_API_VERSION_1_3;

    const char* pInstExt[] = {
#if defined(_DEBUG) && defined(VK_DEBUG_ENABLED)
        VK_EXT_DEBUG_REPORT_EXTENSION_NAME,
#endif
        VK_KHR_SURFACE_EXTENSION_NAME, 
#if defined(_WIN32) && defined(USE_CUDA) && USE_CUDA
        VK_KHR_EXTERNAL_MEMORY_CAPABILITIES_EXTENSION_NAME,
        VK_KHR_EXTERNAL_SEMAPHORE_CAPABILITIES_EXTENSION_NAME,
#endif
        m_winController->getVulkanWindowSurfaceExtension().data()};

#if defined(_DEBUG) && defined(VK_DEBUG_ENABLED)
    const char* pInstLayers[] = {"VK_LAYER_KHRONOS_validation"};
#endif

    VkInstanceCreateInfo instInfo = {};
    instInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instInfo.pApplicationInfo = &appInfo;
#if defined(_DEBUG) && defined(VK_DEBUG_ENABLED)
    instInfo.enabledLayerCount = ARRAY_SIZE_IN_ELEMENTS(pInstLayers);
    instInfo.ppEnabledLayerNames = pInstLayers;
#endif
    instInfo.enabledExtensionCount = ARRAY_SIZE_IN_ELEMENTS(pInstExt);
    instInfo.ppEnabledExtensionNames = pInstExt;

    VkResult res = vkCreateInstance(&instInfo, nullptr, &m_inst);
    CHECK_VULKAN_ERROR("vkCreateInstance %d\n", res);

#if defined(_DEBUG) && defined(VK_DEBUG_ENABLED)
    // Get the address to the vkCreateDebugReportCallbackEXT function
    PFN_vkCreateDebugReportCallbackEXT my_vkCreateDebugReportCallbackEXT =
        reinterpret_cast<PFN_vkCreateDebugReportCallbackEXT>(vkGetInstanceProcAddr(m_inst, "vkCreateDebugReportCallbackEXT"));

    // Register the debug callback
    VkDebugReportCallbackCreateInfoEXT callbackCreateInfo;
    callbackCreateInfo.sType = VK_STRUCTURE_TYPE_DEBUG_REPORT_CREATE_INFO_EXT;
    callbackCreateInfo.pNext = nullptr;
    callbackCreateInfo.flags =
        VK_DEBUG_REPORT_ERROR_BIT_EXT | VK_DEBUG_REPORT_WARNING_BIT_EXT | VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT;
    callbackCreateInfo.pfnCallback = &MyDebugReportCallback;
    callbackCreateInfo.pUserData = nullptr;

    VkDebugReportCallbackEXT callback;
    res = my_vkCreateDebugReportCallbackEXT(m_inst, &callbackCreateInfo, nullptr, &callback);
    CHECK_VULKAN_ERROR("my_vkCreateDebugReportCallbackEXT error %d\n", res);
#endif
}

void VulkanCore::createLogicalDevice() {
    VkDeviceQueueCreateInfo qInfo = {};
    qInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;

    float qPriorities = 1.0f;
    qInfo.queueCount = 1;
    qInfo.pQueuePriorities = &qPriorities;
    qInfo.queueFamilyIndex = m_gfxQueueFamily;

#if defined(_WIN32) && defined(USE_CUDA) && USE_CUDA
    const char* pDevExt[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME, VK_KHR_EXTERNAL_MEMORY_WIN32_EXTENSION_NAME,
                             VK_KHR_EXTERNAL_SEMAPHORE_WIN32_EXTENSION_NAME};
#else
    const char* pDevExt[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
#endif  

    VkPhysicalDeviceFeatures deviceFeatures{};
    deviceFeatures.samplerAnisotropy = VK_TRUE;
    deviceFeatures.tessellationShader = VK_TRUE;

    VkDeviceCreateInfo devInfo = {};
    devInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    devInfo.enabledExtensionCount = ARRAY_SIZE_IN_ELEMENTS(pDevExt);
    devInfo.ppEnabledExtensionNames = pDevExt;
    devInfo.queueCreateInfoCount = 1;
    devInfo.pQueueCreateInfos = &qInfo;
    devInfo.pEnabledFeatures = &deviceFeatures;

    VkResult res = vkCreateDevice(getPhysDevice(), &devInfo, nullptr, &m_device);

    CHECK_VULKAN_ERROR("vkCreateDevice error %d\n", res);

    Utils::printLog(INFO_PARAM, "Device created");
}
