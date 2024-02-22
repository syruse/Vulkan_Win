#include "Utils.h"
#include "Constants.h"
/// declaration for template instatiation
#include "I3DModel.h"
#include "Particle.h"
#include "Skybox.h"

#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <cstring>
#include <fstream>
#include <memory>

namespace Utils {

void printErrorF(const char* pFileName, size_t line, const char* pFuncName, const char* format, ...) {
    char msg[100];
    va_list args;
    va_start(args, format);
    vsnprintf(msg, sizeof(msg), format, args);
    va_end(args);

    printLog(true, "\nin file: ", pFileName, " at line: ", line, " from function: ", pFuncName, " \n ", msg);
}

void printInfoF(const char* pFileName, size_t line, const char* pFuncName, const char* format, ...) {
    char msg[100];
    va_list args;
    va_start(args, format);
    vsnprintf(msg, sizeof(msg), format, args);
    va_end(args);

    printLog(false, "\nin file: ", pFileName, " at line: ", line, " from function: ", pFuncName, " \n ", msg);
}

void VulkanEnumExtProps(std::vector<VkExtensionProperties>& ExtProps) {
    uint32_t NumExt = 0;
    VkResult res = vkEnumerateInstanceExtensionProperties(nullptr, &NumExt, nullptr);
    CHECK_VULKAN_ERROR("vkEnumerateInstanceExtensionProperties error %d\n", res);

    printf("Found %d extensions\n", NumExt);

    ExtProps.resize(NumExt);

    res = vkEnumerateInstanceExtensionProperties(nullptr, &NumExt, &ExtProps[0]);
    CHECK_VULKAN_ERROR("vkEnumerateInstanceExtensionProperties error %d\n", res);

    for (decltype(NumExt) i = 0; i < NumExt; ++i) {
        INFO_FORMAT("Instance extension %d - %s\n", i, ExtProps[i].extensionName);
    }
}

void VulkanPrintImageUsageFlags(const VkImageUsageFlags& flags) {
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

void VulkanGetPhysicalDevices(VkInstance inst, VkSurfaceKHR Surface, VulkanPhysicalDevices& PhysDevices) {
    uint32_t NumDevices = 0;

    VkResult res = vkEnumeratePhysicalDevices(inst, &NumDevices, nullptr);
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
        printf("    API version: %d.%d.%d\n", VK_VERSION_MAJOR(apiVer), VK_VERSION_MINOR(apiVer), VK_VERSION_PATCH(apiVer));
        uint32_t NumQFamily = 0;

        vkGetPhysicalDeviceQueueFamilyProperties(PhysDev, &NumQFamily, nullptr);

        printf("    Num of family queues: %d\n", NumQFamily);

        PhysDevices.m_qFamilyProps[i].resize(NumQFamily);
        PhysDevices.m_qSupportsPresent[i].resize(NumQFamily);

        vkGetPhysicalDeviceQueueFamilyProperties(PhysDev, &NumQFamily, &(PhysDevices.m_qFamilyProps[i][0]));

        for (size_t q = 0; q < NumQFamily; q++) {
            res = vkGetPhysicalDeviceSurfaceSupportKHR(PhysDev, q, Surface, &(PhysDevices.m_qSupportsPresent[i][q]));
            CHECK_VULKAN_ERROR("vkGetPhysicalDeviceSurfaceSupportKHR error %d\n", res);
        }

        uint32_t NumFormats = 0;
        vkGetPhysicalDeviceSurfaceFormatsKHR(PhysDev, Surface, &NumFormats, nullptr);
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

        res = vkGetPhysicalDeviceSurfacePresentModesKHR(PhysDev, Surface, &NumPresentModes, nullptr);
        CHECK_VULKAN_ERROR("vkGetPhysicalDeviceSurfacePresentModesKHR error %d\n", res);

        assert(NumPresentModes != 0);

        printf("Number of presentation modes %d\n", NumPresentModes);
        PhysDevices.m_presentModes[i].resize(NumPresentModes);
        res = vkGetPhysicalDeviceSurfacePresentModesKHR(PhysDev, Surface, &NumPresentModes, &(PhysDevices.m_presentModes[i][0]));
    }
}

size_t VulkanFindMemoryType(VkPhysicalDevice physicalDevice, const VkMemoryRequirements& memRequirements,
                            VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);

    for (size_t i = 0u; i < memProperties.memoryTypeCount; i++) {
        if ((memRequirements.memoryTypeBits & (1 << i)) &&
            (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }

    Utils::printLog(ERROR_PARAM, "failed to find suitable memory type!");
}

void VulkanCreateBuffer(VkDevice device, VkPhysicalDevice physicalDevice, VkDeviceSize size, VkBufferUsageFlags usage,
                        VkMemoryPropertyFlags properties, VkBuffer& buffer, VkDeviceMemory& bufferMemory) {
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(device, &bufferInfo, nullptr, &buffer) != VK_SUCCESS) {
        Utils::printLog(ERROR_PARAM, "failed to create buffer!");
    }

    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(device, buffer, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = Utils::VulkanFindMemoryType(physicalDevice, memRequirements, properties);

    if (vkAllocateMemory(device, &allocInfo, nullptr, &bufferMemory) != VK_SUCCESS) {
        Utils::printLog(ERROR_PARAM, "failed to allocate buffer memory!");
    }

    vkBindBufferMemory(device, buffer, bufferMemory, 0);
}

VkCommandBuffer VulkanBeginSingleTimeCommands(VkDevice device, VkCommandPool cmdBufPool)  /// TO FIX  in one command buffer
{
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = cmdBufPool;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer commandBuffer;
    if (vkAllocateCommandBuffers(device, &allocInfo, &commandBuffer) != VK_SUCCESS) {
        Utils::printLog(ERROR_PARAM, "failed to allocate command buffer!");
    }

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkBeginCommandBuffer(commandBuffer, &beginInfo);

    return commandBuffer;
}

void VulkanEndSingleTimeCommands(VkDevice device, VkQueue queue, VkCommandPool cmdBufPool, VkCommandBuffer* commandBuffer) {
    if (commandBuffer != nullptr && vkEndCommandBuffer(*commandBuffer) != VK_SUCCESS) {
        Utils::printLog(ERROR_PARAM, "failed to end one-off command buffer!");
    }

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = commandBuffer;

    vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(queue);

    vkFreeCommandBuffers(device, cmdBufPool, 1, commandBuffer);
}

void VulkanCopyBuffer(VkDevice device, VkQueue queue, VkCommandPool cmdBufPool, VkBuffer srcBuffer, VkBuffer dstBuffer,
                      VkDeviceSize size) {
    VkCommandBuffer commandBuffer = VulkanBeginSingleTimeCommands(device, cmdBufPool);

    VkBufferCopy copyRegion{};
    copyRegion.size = size;
    vkCmdCopyBuffer(commandBuffer, srcBuffer, dstBuffer, 1, &copyRegion);

    VulkanEndSingleTimeCommands(device, queue, cmdBufPool, &commandBuffer);
}

std::string formPath(std::string_view dir, std::string_view fileName) {
    std::string resultPath;
    resultPath.reserve(dir.length() + fileName.length() + 1);
    resultPath = std::string{dir};
    resultPath += Constants::DIR_SEPARATOR;
    resultPath += std::string{fileName};

    return resultPath;
}

VkShaderModule VulkanCreateShaderModule(VkDevice device, std::string_view fileName) {
    std::string shaderPath = formPath(Constants::SHADERS_DIR, fileName);

    assert(shaderPath.c_str());
    std::ifstream file(shaderPath.c_str(), std::ios::ate | std::ios::binary);
    if (!file.is_open()) {
        Utils::printLog(ERROR_PARAM, shaderPath.c_str());
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
    VkResult res = vkCreateShaderModule(device, &shaderCreateInfo, nullptr, &shaderModule);
    CHECK_VULKAN_ERROR("vkCreateShaderModule error %d\n", res);
    Utils::printLog(INFO_PARAM, "Created shader ", shaderPath.c_str());
    return shaderModule;
}

void VulkanCheckValidationLayerSupport() {
    uint32_t layerCount;
    vkEnumerateInstanceLayerProperties(&layerCount, nullptr);

    std::vector<VkLayerProperties> availableLayers(layerCount);
    vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());

    for (const auto& layer : availableLayers) {
        Utils::printLog(INFO_PARAM, layer.layerName);
    }
}

VkResult VulkanCreateImage(VkDevice device, VkPhysicalDevice physicalDevice, uint32_t width, uint32_t height, VkFormat format,
                           VkImageTiling tiling, VkImageUsageFlags usage, VkMemoryPropertyFlags properties, VkImage& image,
                           VkDeviceMemory& imageMemory, uint32_t mipLevels, uint32_t arrayLayers) {
    VkResult res;

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = width;
    imageInfo.extent.height = height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = mipLevels;
    imageInfo.arrayLayers = arrayLayers;
    imageInfo.format = format;
    imageInfo.tiling = tiling;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = usage;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (arrayLayers == 6u) {
        imageInfo.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
    }

    res = vkCreateImage(device, &imageInfo, nullptr, &image);
    if (res != VK_SUCCESS) {
        Utils::printLog(ERROR_PARAM, "failed to create image: ", res);
        return res;
    }

    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(device, image, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = VulkanFindMemoryType(physicalDevice, memRequirements, properties);

    res = vkAllocateMemory(device, &allocInfo, nullptr, &imageMemory);
    if (res != VK_SUCCESS) {
        Utils::printLog(ERROR_PARAM, "failed to allocate image memory: ", res);
        return res;
    }

    res = vkBindImageMemory(device, image, imageMemory, 0);

    return res;
}

void VulkanImageMemoryBarrier(VkCommandBuffer commandBuffer, VkImage image, VkFormat format, VkImageLayout oldLayout,
                              VkImageLayout newLayout, VkImageAspectFlags aspectMask, uint32_t mipLevels, uint32_t layersCount,
                              VkAccessFlags srcAccessMask, VkAccessFlags dstAccessMask, VkPipelineStageFlags sourceStage,
                              VkPipelineStageFlags destinationStage) {
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = aspectMask;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = mipLevels;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = layersCount;
    barrier.srcAccessMask = srcAccessMask;
    barrier.dstAccessMask = dstAccessMask;

    vkCmdPipelineBarrier(commandBuffer, sourceStage, destinationStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
}

void VulkanTransitionImageLayout(VkDevice device, VkQueue queue, VkCommandPool cmdBufPool, VkImage image, VkFormat format,
                                 VkImageLayout oldLayout, VkImageLayout newLayout, VkImageAspectFlags aspectMask,
                                 uint32_t mipLevels, uint32_t layersCount) {
    VkCommandBuffer commandBuffer = VulkanBeginSingleTimeCommands(device, cmdBufPool);

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = aspectMask;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = mipLevels;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = layersCount;

    VkPipelineStageFlags sourceStage;
    VkPipelineStageFlags destinationStage;

    if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

        sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    } else {
        Utils::printLog(ERROR_PARAM, "unsupported layout transition!");
    }

    vkCmdPipelineBarrier(commandBuffer, sourceStage, destinationStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);

    VulkanEndSingleTimeCommands(device, queue, cmdBufPool, &commandBuffer);
}

void VulkanCopyBufferToImage(VkDevice device, VkQueue queue, VkCommandPool cmdBufPool, VkBuffer buffer, VkImage image,
                             uint32_t width, uint32_t height, uint32_t layersCount) {
    VkCommandBuffer commandBuffer = VulkanBeginSingleTimeCommands(device, cmdBufPool);

    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = layersCount;
    region.imageOffset = {0, 0, 0};
    region.imageExtent = {width, height, 1};

    vkCmdCopyBufferToImage(commandBuffer, buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    VulkanEndSingleTimeCommands(device, queue, cmdBufPool, &commandBuffer);
}

void VulkanGenerateMipmaps(VkDevice device, VkQueue queue, VkCommandPool cmdBufPool, VkImage image, VkFormat imageFormat,
                           int16_t texWidth, int16_t texHeight, uint8_t mipLevels, uint8_t layersAmount) {
    VkCommandBuffer commandBuffer = VulkanBeginSingleTimeCommands(device, cmdBufPool);

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.image = image;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = layersAmount;
    barrier.subresourceRange.levelCount = 1;

    int32_t mipWidth = texWidth;
    int32_t mipHeight = texHeight;

    for (uint32_t i = 1; i < mipLevels; i++) {
        barrier.subresourceRange.baseMipLevel = i - 1;
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

        vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
                             nullptr, 1, &barrier);

        VkImageBlit blit{};
        blit.srcOffsets[0] = {0, 0, 0};
        blit.srcOffsets[1] = {mipWidth, mipHeight, 1};
        blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blit.srcSubresource.mipLevel = i - 1;
        blit.srcSubresource.baseArrayLayer = 0;
        blit.srcSubresource.layerCount = layersAmount;
        blit.dstOffsets[0] = {0, 0, 0};
        blit.dstOffsets[1] = {mipWidth > 1 ? mipWidth / 2 : 1, mipHeight > 1 ? mipHeight / 2 : 1, 1};
        blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blit.dstSubresource.mipLevel = i;
        blit.dstSubresource.baseArrayLayer = 0;
        blit.dstSubresource.layerCount = layersAmount;

        vkCmdBlitImage(commandBuffer, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
                       &blit, VK_FILTER_LINEAR);

        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr,
                             0, nullptr, 1, &barrier);

        if (mipWidth > 1)
            mipWidth /= 2;
        if (mipHeight > 1)
            mipHeight /= 2;
    }

    barrier.subresourceRange.baseMipLevel = mipLevels - 1;
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0,
                         nullptr, 1, &barrier);

    VulkanEndSingleTimeCommands(device, queue, cmdBufPool, &commandBuffer);
}

VkResult VulkanCreateImageView(VkDevice device, VkImage image, VkFormat format, VkImageAspectFlags aspectMask,
                               VkImageView& imageView, uint32_t mipLevels, VkImageViewType type, uint32_t layersCount) {
    VkResult res;

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = image;
    viewInfo.viewType = type;
    viewInfo.format = format;
    viewInfo.subresourceRange.aspectMask = aspectMask;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = mipLevels;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = layersCount;

    res = vkCreateImageView(device, &viewInfo, nullptr, &imageView);

    Utils::printLog(INFO_PARAM, "creation texture image view code ", res);

    return res;
}

bool VulkanFindSupportedFormat(VkPhysicalDevice physicalDevice, const std::vector<VkFormat>& candidates, VkImageTiling tiling,
                               VkFormatFeatureFlags features, VkFormat& ret_format) {
    bool status = false;
    for (VkFormat format : candidates) {
        VkFormatProperties props;
        vkGetPhysicalDeviceFormatProperties(physicalDevice, format, &props);

        if (tiling == VK_IMAGE_TILING_LINEAR && (props.linearTilingFeatures & features) == features) {
            ret_format = format;
            status = true;
        } else if (tiling == VK_IMAGE_TILING_OPTIMAL && (props.optimalTilingFeatures & features) == features) {
            ret_format = format;
            status = true;
        }
    }

    return status;
}

template <class T>
void createGeneralBuffer(VkDevice device, VkPhysicalDevice physicalDevice, VkCommandPool cmdBufPool, VkQueue queue,
                         const std::vector<uint32_t>& indices, const std::vector<T>& vertices, VkDeviceSize& verticesBufferOffset,
                         VkBuffer& generalBuffer, VkDeviceMemory& generalBufferMemory) {
    /** Note: general buffer keeping both geometry data,
              index buffer is placed first and next to index buffer the vertex buffer placed
    */
    const VkDeviceSize indicesSize = sizeof(indices[0]) * indices.size();
    verticesBufferOffset = indicesSize;
    const VkDeviceSize verticesSize = sizeof(vertices[0]) * vertices.size();
    const VkDeviceSize bufferSize = indicesSize + verticesSize;

    VkBuffer stagingBuffer;
    VkDeviceMemory stagingBufferMemory;
    Utils::VulkanCreateBuffer(device, physicalDevice, bufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                              VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, stagingBuffer,
                              stagingBufferMemory);

    void* data;
    vkMapMemory(device, stagingBufferMemory, 0, bufferSize, 0, &data);
    memcpy(data, indices.data(), (size_t)indicesSize);
    memcpy((char*)data + verticesBufferOffset, vertices.data(), verticesSize);
    vkUnmapMemory(device, stagingBufferMemory);

    Utils::VulkanCreateBuffer(
        device, physicalDevice, bufferSize,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, generalBuffer, generalBufferMemory);

    Utils::VulkanCopyBuffer(device, queue, cmdBufPool, stagingBuffer, generalBuffer, bufferSize);

    vkDestroyBuffer(device, stagingBuffer, nullptr);
    vkFreeMemory(device, stagingBufferMemory, nullptr);
}

// Explicit template instantiation
template void createGeneralBuffer<I3DModel::Vertex>(VkDevice device, VkPhysicalDevice physicalDevice, VkCommandPool cmdBufPool,
                                                    VkQueue queue, const std::vector<uint32_t>& indices,
                                                    const std::vector<I3DModel::Vertex>& vertices,
                                                    VkDeviceSize& verticesBufferOffset, VkBuffer& generalBuffer,
                                                    VkDeviceMemory& generalBufferMemory);
template void createGeneralBuffer<Skybox::Vertex>(VkDevice device, VkPhysicalDevice physicalDevice, VkCommandPool cmdBufPool,
                                                  VkQueue queue, const std::vector<uint32_t>& indices,
                                                  const std::vector<Skybox::Vertex>& vertices, VkDeviceSize& verticesBufferOffset,
                                                  VkBuffer& generalBuffer, VkDeviceMemory& generalBufferMemory);
template void createGeneralBuffer<Particle::Instance>(VkDevice device, VkPhysicalDevice physicalDevice, VkCommandPool cmdBufPool,
                                                      VkQueue queue, const std::vector<uint32_t>& indices,
                                                      const std::vector<Particle::Instance>& vertices,
                                                      VkDeviceSize& verticesBufferOffset, VkBuffer& generalBuffer,
                                                      VkDeviceMemory& generalBufferMemory);

}  // namespace Utils
