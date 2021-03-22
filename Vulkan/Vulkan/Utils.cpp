#include "Utils.h"
#include <windows.h>
#include <stdio.h>
#include <assert.h>
#include <fstream>

namespace Utils {

    void printErrorF(const char* pFileName, size_t line, const char* pFuncName, const char* format, ...)
    {
        char msg[1000];
        va_list args;
        va_start(args, format);
        vsnprintf_s(msg, sizeof(msg), format, args);
        va_end(args);

        printLog(true, "\nin file: ", pFileName, " at line: ", line, " from function: ", pFuncName, " \n ", msg);
    }

    void printInfoF(const char* pFileName, size_t line, const char* pFuncName, const char* format, ...)
    {
        char msg[1000];
        va_list args;
        va_start(args, format);
        vsnprintf_s(msg, sizeof(msg), format, args);
        va_end(args);

        printLog(false, "\nin file: ", pFileName, " at line: ", line, " from function: ", pFuncName, " \n ", msg);
    }

    void VulkanEnumExtProps(std::vector<VkExtensionProperties>& ExtProps)
    {
        uint32_t NumExt = 0;
        VkResult res = vkEnumerateInstanceExtensionProperties(NULL, &NumExt, NULL);
        CHECK_VULKAN_ERROR("vkEnumerateInstanceExtensionProperties error %d\n", res);

        printf("Found %d extensions\n", NumExt);

        ExtProps.resize(NumExt);

        res = vkEnumerateInstanceExtensionProperties(NULL, &NumExt, &ExtProps[0]);
        CHECK_VULKAN_ERROR("vkEnumerateInstanceExtensionProperties error %d\n", res);

        for (decltype(NumExt) i = 0; i < NumExt; ++i) {
            INFO("Instance extension %d - %s\n", i, ExtProps[i].extensionName);
        }
    }

    void VulkanPrintImageUsageFlags(const VkImageUsageFlags& flags)
    {
        if (flags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) {
            printf("Image usage transfer src is supported\n");
        }

        if (flags & VK_IMAGE_USAGE_TRANSFER_DST_BIT) {
            printf("Image usage transfer dest is supported\n");
        }

        if (flags & VK_IMAGE_USAGE_SAMPLED_BIT) {
            printf("Image usage sampled is supported\n");
        }

        if (flags & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) {
            printf("Image usage color attachment is supported\n");
        }

        if (flags & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) {
            printf("Image usage depth stencil attachment is supported\n");
        }

        if (flags & VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT) {
            printf("Image usage transient attachment is supported\n");
        }

        if (flags & VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT) {
            printf("Image usage input attachment is supported\n");
        }
    }


    void VulkanGetPhysicalDevices(const VkInstance& inst, const VkSurfaceKHR& Surface, VulkanPhysicalDevices& PhysDevices)
    {
        uint32_t NumDevices = 0;

        VkResult res = vkEnumeratePhysicalDevices(inst, &NumDevices, NULL);
        CHECK_VULKAN_ERROR("vkEnumeratePhysicalDevices error %d\n", res);

        printf("Num physical devices %d\n", NumDevices);

        PhysDevices.m_devices.resize(NumDevices);
        PhysDevices.m_devProps.resize(NumDevices);
        PhysDevices.m_qFamilyProps.resize(NumDevices);
        PhysDevices.m_qSupportsPresent.resize(NumDevices);
        PhysDevices.m_surfaceFormats.resize(NumDevices);
        PhysDevices.m_surfaceCaps.resize(NumDevices);
        PhysDevices.m_presentModes.resize(NumDevices);

        res = vkEnumeratePhysicalDevices(inst, &NumDevices, &PhysDevices.m_devices[0]);
        CHECK_VULKAN_ERROR("vkEnumeratePhysicalDevices error %d\n", res);

        for (size_t i = 0; i < NumDevices; ++i) {
            const VkPhysicalDevice& PhysDev = PhysDevices.m_devices[i];
            vkGetPhysicalDeviceProperties(PhysDev, &PhysDevices.m_devProps[i]);

            printf("Device name: %s\n", PhysDevices.m_devProps[i].deviceName);
            uint32_t apiVer = PhysDevices.m_devProps[i].apiVersion;
            printf("    API version: %d.%d.%d\n", VK_VERSION_MAJOR(apiVer),
                VK_VERSION_MINOR(apiVer),
                VK_VERSION_PATCH(apiVer));
            uint32_t NumQFamily = 0;

            vkGetPhysicalDeviceQueueFamilyProperties(PhysDev, &NumQFamily, NULL);

            printf("    Num of family queues: %d\n", NumQFamily);

            PhysDevices.m_qFamilyProps[i].resize(NumQFamily);
            PhysDevices.m_qSupportsPresent[i].resize(NumQFamily);

            vkGetPhysicalDeviceQueueFamilyProperties(PhysDev, &NumQFamily, &(PhysDevices.m_qFamilyProps[i][0]));

            for (size_t q = 0; q < NumQFamily; q++) {
                res = vkGetPhysicalDeviceSurfaceSupportKHR(PhysDev, q, Surface, &(PhysDevices.m_qSupportsPresent[i][q]));
                CHECK_VULKAN_ERROR("vkGetPhysicalDeviceSurfaceSupportKHR error %d\n", res);
            }

            uint32_t NumFormats = 0;
            vkGetPhysicalDeviceSurfaceFormatsKHR(PhysDev, Surface, &NumFormats, NULL);
            assert(NumFormats > 0);

            PhysDevices.m_surfaceFormats[i].resize(NumFormats);

            res = vkGetPhysicalDeviceSurfaceFormatsKHR(PhysDev, Surface, &NumFormats, &(PhysDevices.m_surfaceFormats[i][0]));
            CHECK_VULKAN_ERROR("vkGetPhysicalDeviceSurfaceFormatsKHR error %d\n", res);

            for (size_t j = 0; j < NumFormats; j++) {
                const VkSurfaceFormatKHR& SurfaceFormat = PhysDevices.m_surfaceFormats[i][j];
                printf("    Format %d color space %d\n", SurfaceFormat.format, SurfaceFormat.colorSpace);
            }

            res = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(PhysDev, Surface, &(PhysDevices.m_surfaceCaps[i]));
            CHECK_VULKAN_ERROR("vkGetPhysicalDeviceSurfaceCapabilitiesKHR error %d\n", res);

            VulkanPrintImageUsageFlags(PhysDevices.m_surfaceCaps[i].supportedUsageFlags);

            uint32_t NumPresentModes = 0;

            res = vkGetPhysicalDeviceSurfacePresentModesKHR(PhysDev, Surface, &NumPresentModes, NULL);
            CHECK_VULKAN_ERROR("vkGetPhysicalDeviceSurfacePresentModesKHR error %d\n", res);

            assert(NumPresentModes != 0);

            printf("Number of presentation modes %d\n", NumPresentModes);
            PhysDevices.m_presentModes[i].resize(NumPresentModes);
            res = vkGetPhysicalDeviceSurfacePresentModesKHR(PhysDev, Surface, &NumPresentModes, &(PhysDevices.m_presentModes[i][0]));
        }
    }

    size_t VulkanFindMemoryType(const VkPhysicalDevice& physicalDevice, const VkMemoryRequirements& memRequirements, VkMemoryPropertyFlags properties)
    {
        VkPhysicalDeviceMemoryProperties memProperties;
        vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);

        for (size_t i = 0u; i < memProperties.memoryTypeCount; i++) {
            if ((memRequirements.memoryTypeBits & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
                return i;
            }
        }

        ERROR("failed to find suitable memory type!");
    }

    void VulkanÑreateBuffer(const VkDevice& device, const VkPhysicalDevice& physicalDevice, VkDeviceSize size, VkBufferUsageFlags usage,
        VkMemoryPropertyFlags properties, VkBuffer& buffer, VkDeviceMemory& bufferMemory) 
    {
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = size;
        bufferInfo.usage = usage;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        if (vkCreateBuffer(device, &bufferInfo, nullptr, &buffer) != VK_SUCCESS) {
            ERROR("failed to create buffer!");
        }

        VkMemoryRequirements memRequirements;
        vkGetBufferMemoryRequirements(device, buffer, &memRequirements);

        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memRequirements.size;
        allocInfo.memoryTypeIndex = Utils::VulkanFindMemoryType(physicalDevice, memRequirements, properties);

        if (vkAllocateMemory(device, &allocInfo, nullptr, &bufferMemory) != VK_SUCCESS) {
            ERROR("failed to allocate buffer memory!");
        }

        vkBindBufferMemory(device, buffer, bufferMemory, 0);
    }

    void VulkanÑopyBuffer(VkDevice device, VkQueue queue, VkCommandPool cmdBufPool, VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size)
    {
        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandPool = cmdBufPool;
        allocInfo.commandBufferCount = 1;

        VkCommandBuffer commandBuffer;
        vkAllocateCommandBuffers(device, &allocInfo, &commandBuffer);

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        vkBeginCommandBuffer(commandBuffer, &beginInfo);

        VkBufferCopy copyRegion{};
        copyRegion.size = size;
        vkCmdCopyBuffer(commandBuffer, srcBuffer, dstBuffer, 1, &copyRegion);

        vkEndCommandBuffer(commandBuffer);

        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &commandBuffer;

        vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE);
        vkQueueWaitIdle(queue);

        vkFreeCommandBuffers(device, cmdBufPool, 1, &commandBuffer);
    }

    VkShaderModule VulkanCreateShaderModule(VkDevice& device, std::string_view fileName)
    {
        assert(fileName.data());
        std::ifstream file(fileName.data(), std::ios::ate | std::ios::binary);
        if (!file.is_open()) {
            ERROR(fileName.data());
        }
        size_t codeSize = (size_t)file.tellg();
        assert(codeSize);

        std::vector<char> buffer(codeSize);
        file.seekg(0);
        file.read(buffer.data(), codeSize);
        file.close();

        VkShaderModuleCreateInfo shaderCreateInfo = {};
        shaderCreateInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        shaderCreateInfo.codeSize = codeSize;
        shaderCreateInfo.pCode = reinterpret_cast<const uint32_t*>(buffer.data());

        VkShaderModule shaderModule;
        VkResult res = vkCreateShaderModule(device, &shaderCreateInfo, NULL, &shaderModule);
        CHECK_VULKAN_ERROR("vkCreateShaderModule error %d\n", res);
        INFO("Created shader %s \n", fileName.data());
        return shaderModule;
    }

    void VulkanCheckValidationLayerSupport() 
    {
        uint32_t layerCount;
        vkEnumerateInstanceLayerProperties(&layerCount, nullptr);

        std::vector<VkLayerProperties> availableLayers(layerCount);
        vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());

        for (const auto& layer : availableLayers) 
        {
            INFO(layer.layerName);
        }
    }

}
