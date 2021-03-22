#include "VulkanCore.h"
#include "Utils.h"
#include <vector>
#include <cassert>
#include "IControl.h"

using namespace Utils;

PFN_vkCreateDebugReportCallbackEXT my_vkCreateDebugReportCallbackEXT = NULL;

VKAPI_ATTR VkBool32 VKAPI_CALL MyDebugReportCallback(
    VkDebugReportFlagsEXT       flags,
    VkDebugReportObjectTypeEXT  objectType,
    uint64_t                    object,
    size_t                      location,
    int32_t                     messageCode,
    const char* pLayerPrefix,
    const char* pMessage,
    void* pUserData)
{
    INFO("%s\n", pMessage);
    return VK_FALSE;
}


VulkanCore::VulkanCore(std::unique_ptr<IControl>&& winController)
    : m_winController(std::move(winController))
{
}


void VulkanCore::init()
{
    assert(m_winController);

    m_winController->init();

    std::vector<VkExtensionProperties> ExtProps;
    VulkanEnumExtProps(ExtProps);

    createInstance();

    m_surface = createSurface(m_inst);
    assert(m_surface);

    INFO("Surface created\n");

    VulkanGetPhysicalDevices(m_inst, m_surface, m_physDevices);
    selectPhysicalDevice();
    createLogicalDevice();
}

VkSurfaceKHR VulkanCore::createSurface(VkInstance& inst)
{
    assert(m_winController);
    return m_winController->createSurface(inst);
}

const VkPhysicalDevice& VulkanCore::getPhysDevice() const
{
    assert(m_gfxDevIndex >= 0);
    return m_physDevices.m_devices[m_gfxDevIndex];
}

const VkSurfaceFormatKHR& VulkanCore::getSurfaceFormat() const
{
    assert(m_gfxDevIndex >= 0);
    return m_physDevices.m_surfaceFormats[m_gfxDevIndex][0];
}


const VkSurfaceCapabilitiesKHR VulkanCore::getSurfaceCaps() const
{
    assert(m_gfxDevIndex >= 0);
    return m_physDevices.m_surfaceCaps[m_gfxDevIndex];

}


void VulkanCore::selectPhysicalDevice()
{
    for (size_t i = 0; i < m_physDevices.m_devices.size(); ++i) {

        for (size_t j = 0; j < m_physDevices.m_qFamilyProps[i].size(); ++j) {
            VkQueueFamilyProperties& QFamilyProp = m_physDevices.m_qFamilyProps[i][j];

            INFO("Family %d Num queues: %d\n", j, QFamilyProp.queueCount);
            VkQueueFlags flags = QFamilyProp.queueFlags;
            INFO("    GFX %s, Compute %s, Transfer %s, Sparse binding %s\n",
                (flags & VK_QUEUE_GRAPHICS_BIT) ? "Yes" : "No",
                (flags & VK_QUEUE_COMPUTE_BIT) ? "Yes" : "No",
                (flags & VK_QUEUE_TRANSFER_BIT) ? "Yes" : "No",
                (flags & VK_QUEUE_SPARSE_BINDING_BIT) ? "Yes" : "No");

            if ((flags & VK_QUEUE_GRAPHICS_BIT) && (m_gfxDevIndex == -1)) {
                if (!m_physDevices.m_qSupportsPresent[i][j]) {
                    INFO("Present is not supported\n");
                    continue;
                }

                m_gfxDevIndex = i;
                m_gfxQueueFamily = j;
                INFO("Using GFX device %d and queue family %d\n", m_gfxDevIndex, m_gfxQueueFamily);
            }
        }
    }

    if (m_gfxDevIndex == -1) {
        INFO("No GFX device found!\n");
        assert(0);
    }
}


void VulkanCore::createInstance()
{
    assert(m_winController);
    const std::wstring appNameW(m_winController->getAppName().data());
    const std::string appName(appNameW.begin(), appNameW.end());

    VkApplicationInfo appInfo = {};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = appName.c_str();
    appInfo.engineVersion = 1;
    appInfo.apiVersion = VK_API_VERSION_1_2;

    const char* pInstExt[] = {
#ifdef _DEBUG
        VK_EXT_DEBUG_REPORT_EXTENSION_NAME,
#endif        
        VK_KHR_SURFACE_EXTENSION_NAME,
#ifdef _WIN32    
        VK_KHR_WIN32_SURFACE_EXTENSION_NAME,
#else    
        VK_KHR_XCB_SURFACE_EXTENSION_NAME
#endif            
    };

#ifdef _DEBUG
    const char* pInstLayers[] = {
        "VK_LAYER_KHRONOS_validation"
    };
#endif    

    VkInstanceCreateInfo instInfo = {};
    instInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instInfo.pApplicationInfo = &appInfo;
#ifdef ENABLE_DEBUG_LAYERS    
    instInfo.enabledLayerCount = ARRAY_SIZE_IN_ELEMENTS(pInstLayers);
    instInfo.ppEnabledLayerNames = pInstLayers;
#endif    
    instInfo.enabledExtensionCount = ARRAY_SIZE_IN_ELEMENTS(pInstExt);
    instInfo.ppEnabledExtensionNames = pInstExt;

    VkResult res = vkCreateInstance(&instInfo, NULL, &m_inst);
    CHECK_VULKAN_ERROR("vkCreateInstance %d\n", res);

#ifdef _DEBUG
    // Get the address to the vkCreateDebugReportCallbackEXT function
    my_vkCreateDebugReportCallbackEXT = reinterpret_cast<PFN_vkCreateDebugReportCallbackEXT>(vkGetInstanceProcAddr(m_inst, "vkCreateDebugReportCallbackEXT"));

    // Register the debug callback
    VkDebugReportCallbackCreateInfoEXT callbackCreateInfo;
    callbackCreateInfo.sType = VK_STRUCTURE_TYPE_DEBUG_REPORT_CREATE_INFO_EXT;
    callbackCreateInfo.pNext = NULL;
    callbackCreateInfo.flags = VK_DEBUG_REPORT_ERROR_BIT_EXT |
        VK_DEBUG_REPORT_WARNING_BIT_EXT |
        VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT;
    callbackCreateInfo.pfnCallback = &MyDebugReportCallback;
    callbackCreateInfo.pUserData = NULL;

    VkDebugReportCallbackEXT callback;
    res = my_vkCreateDebugReportCallbackEXT(m_inst, &callbackCreateInfo, NULL, &callback);
    CHECK_VULKAN_ERROR("my_vkCreateDebugReportCallbackEXT error %d\n", res);
#endif    
}


void VulkanCore::createLogicalDevice()
{
    VkDeviceQueueCreateInfo qInfo = {};
    qInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;

    float qPriorities = 1.0f;
    qInfo.queueCount = 1;
    qInfo.pQueuePriorities = &qPriorities;
    qInfo.queueFamilyIndex = m_gfxQueueFamily;

    const char* pDevExt[] = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME
    };

    VkDeviceCreateInfo devInfo = {};
    devInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    devInfo.enabledExtensionCount = ARRAY_SIZE_IN_ELEMENTS(pDevExt);
    devInfo.ppEnabledExtensionNames = pDevExt;
    devInfo.queueCreateInfoCount = 1;
    devInfo.pQueueCreateInfos = &qInfo;

    VkResult res = vkCreateDevice(getPhysDevice(), &devInfo, NULL, &m_device);

    CHECK_VULKAN_ERROR("vkCreateDevice error %d\n", res);

    INFO("Device created\n");
}
