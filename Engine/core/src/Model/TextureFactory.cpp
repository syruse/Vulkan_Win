#include "TextureFactory.h"
#include "Constants.h"
#include "Utils.h"

#include <algorithm>
#include <assert.h>
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

static constexpr VkFormat IMAGE_FORMAT = VK_FORMAT_R8G8B8A8_SRGB;

TextureFactory::TextureFactory(const VulkanState& vulkanState) noexcept(true) : m_vkState(vulkanState) {
    mTextureDeleter = [this](TextureFactory::Texture* p) {
        auto p_devide = m_vkState._core.getDevice();
        assert(p_devide);
        Utils::printLog(INFO_PARAM, "texture resources removal");
        vkDestroyImageView(p_devide, p->m_textureImageView, nullptr);
        vkDestroyImage(p_devide, p->m_textureImage, nullptr);
        vkFreeMemory(p_devide, p->m_textureImageMemory, nullptr);
        delete p;
    };
}

TextureFactory::~TextureFactory() {
    // Wait until no actions being run on device before destroying
    std::ignore = vkDeviceWaitIdle(m_vkState._core.getDevice());
    for (const auto& [key, value] : m_samplers) {
        Utils::printLog(INFO_PARAM, "sampler removal with miplevels: ", key);
        vkDestroySampler(m_vkState._core.getDevice(), value, nullptr);
    }
}

void TextureFactory::init() {
    vkGetPhysicalDeviceProperties(m_vkState._core.getPhysDevice(), &m_properties);
    Utils::printLog(INFO_PARAM, "maxSamplerAnisotrop: ", m_properties.limits.maxSamplerAnisotropy);
}

std::weak_ptr<TextureFactory::Texture> TextureFactory::createCubeTexture(const std::array<std::string_view, 6>& textureFileNames,
                                                                         bool is_flippingVertically, bool useTransferQueue) {
    // Prefix the cache key so the same filename requested as a different view type never aliases.
    auto id = std::string{"CUBE:"} + std::string{textureFileNames[0]};

    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (auto it = m_textures.find(id); it != m_textures.end()) {
        return it->second;
    } else {
        std::shared_ptr<TextureFactory::Texture> texture(new TextureFactory::Texture(), mTextureDeleter);

        std::vector<std::string> texturePaths{Utils::formPath(Constants::TEXTURES_DIR, textureFileNames[0]),
                                              Utils::formPath(Constants::TEXTURES_DIR, textureFileNames[1]),
                                              Utils::formPath(Constants::TEXTURES_DIR, textureFileNames[2]),
                                              Utils::formPath(Constants::TEXTURES_DIR, textureFileNames[3]),
                                              Utils::formPath(Constants::TEXTURES_DIR, textureFileNames[4]),
                                              Utils::formPath(Constants::TEXTURES_DIR, textureFileNames[5])};

        const VkQueue queue = useTransferQueue ? m_vkState._transferQueue : m_vkState._queue;
        const VkCommandPool cmdBufPool = useTransferQueue ? m_vkState._transferCmdBufPool : m_vkState._cmdBufPool;
        if (loadImages(*texture, texturePaths, m_vkState._core.getDevice(), m_vkState._core.getPhysDevice(), queue,
                       cmdBufPool, false, is_flippingVertically, useTransferQueue) != VK_SUCCESS) {
            Utils::printLog(ERROR_PARAM, "failed to create cubic texture image ");
        }
        if (Utils::VulkanCreateImageView(m_vkState._core.getDevice(), texture->m_textureImage, IMAGE_FORMAT,
                                         VK_IMAGE_ASPECT_COLOR_BIT, texture->m_textureImageView, 1U, VK_IMAGE_VIEW_TYPE_CUBE,
                                         textureFileNames.size()) != VK_SUCCESS) {
            Utils::printLog(ERROR_PARAM, "failed to create cubic texture imageView ");
        }

        /// Note: creation of sampler in advance
        getTextureSampler(texture->mipLevels);
        m_textures.try_emplace(id, texture);

        return texture;
    }
}

std::weak_ptr<TextureFactory::Texture> TextureFactory::create2DArrayTexture(std::vector<std::string>&& textureFileNames,
                                                                            bool is_miplevelsEnabling,
                                                                            bool is_flippingVertically,
                                                                            bool forceArrayView,
                                                                            bool useTransferQueue) {
    std::vector<std::string> filePaths = std::move(textureFileNames);
    const bool isArrayView = (filePaths.size() > 1) || forceArrayView;
    // Tag the cache key with the resolved view type so 2D vs 2D-array requests for the same filename never alias.
    auto id = std::string{isArrayView ? "ARR:" : "2D:"} + filePaths[0];

    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (auto it = m_textures.find(id); it != m_textures.end()) {
        return it->second;
    } else {
        std::shared_ptr<TextureFactory::Texture> texture(new TextureFactory::Texture(), mTextureDeleter);

        for (auto& filePath : filePaths) {
            filePath = Utils::formPath(Constants::TEXTURES_DIR, filePath);
        }

        // On the graphics queue mips are blitted on the GPU as before; on the transfer queue
        // loadImages() falls back to a CPU-generated mip chain (see below).
        const VkQueue queue = useTransferQueue ? m_vkState._transferQueue : m_vkState._queue;
        const VkCommandPool cmdBufPool = useTransferQueue ? m_vkState._transferCmdBufPool : m_vkState._cmdBufPool;
        if (loadImages(*texture, filePaths, m_vkState._core.getDevice(), m_vkState._core.getPhysDevice(), queue,
                       cmdBufPool, is_miplevelsEnabling, is_flippingVertically, useTransferQueue) != VK_SUCCESS) {
            Utils::printLog(ERROR_PARAM, "failed to create cubic texture image ");
        }
        VkImageViewType viewType = isArrayView ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D;
        if (Utils::VulkanCreateImageView(m_vkState._core.getDevice(), texture->m_textureImage, IMAGE_FORMAT,
                                         VK_IMAGE_ASPECT_COLOR_BIT, texture->m_textureImageView, texture->mipLevels,
                                         viewType, filePaths.size()) != VK_SUCCESS) {
            Utils::printLog(ERROR_PARAM, "failed to create 2DArray texture imageView ");
        }

        /// Note: creation of sampler in advance
        getTextureSampler(texture->mipLevels);
        m_textures.try_emplace(id, texture);

        return texture;
    }
}

std::weak_ptr<TextureFactory::Texture> TextureFactory::create2DTexture(std::string_view pTextureFileName,
                                                                       bool is_miplevelsEnabling, bool is_flippingVertically,
                                                                       bool useTransferQueue) {
    // Prefix the cache key so the same filename requested as a different view type never aliases.
    std::string id = std::string{"2D:"} + std::string{pTextureFileName};
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (auto it = m_textures.find(id); it != m_textures.end()) {
        return it->second;
    } else {
        std::shared_ptr<TextureFactory::Texture> texture(new TextureFactory::Texture(), mTextureDeleter);

        std::string texturePath = Utils::formPath(Constants::TEXTURES_DIR, pTextureFileName);

        // On the graphics queue mips are blitted on the GPU as before; on the transfer queue
        // loadImages() falls back to a CPU-generated mip chain (see below).
        const VkQueue queue = useTransferQueue ? m_vkState._transferQueue : m_vkState._queue;
        const VkCommandPool cmdBufPool = useTransferQueue ? m_vkState._transferCmdBufPool : m_vkState._cmdBufPool;
        if (loadImages(*texture, {texturePath}, m_vkState._core.getDevice(), m_vkState._core.getPhysDevice(), queue,
                       cmdBufPool, is_miplevelsEnabling, is_flippingVertically, useTransferQueue) != VK_SUCCESS) {
            Utils::printLog(ERROR_PARAM, "failed to create texture image ");
        }
        if (Utils::VulkanCreateImageView(m_vkState._core.getDevice(), texture->m_textureImage, IMAGE_FORMAT,
                                         VK_IMAGE_ASPECT_COLOR_BIT, texture->m_textureImageView,
                                         texture->mipLevels) != VK_SUCCESS) {
            Utils::printLog(ERROR_PARAM, "failed to create texture imageView ");
        }

        /// Note: creation of sampler in advance
        getTextureSampler(texture->mipLevels);
        m_textures.try_emplace(id, texture);

        return texture;
    }
}

VkSampler TextureFactory::getTextureSampler(uint32_t mipLevels) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (m_samplers.find(mipLevels) == m_samplers.end()) {
        VkSampler sampler = nullptr;
        VkSamplerCreateInfo samplerInfo{};
        samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samplerInfo.magFilter = VK_FILTER_LINEAR;
        samplerInfo.minFilter = VK_FILTER_LINEAR;
        samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        samplerInfo.anisotropyEnable = (m_properties.limits.maxSamplerAnisotropy < 1 ? VK_FALSE : VK_TRUE);
        samplerInfo.maxAnisotropy = m_properties.limits.maxSamplerAnisotropy;
        samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
        samplerInfo.unnormalizedCoordinates = VK_FALSE;  /// -> [0: 1]
        samplerInfo.compareEnable = VK_FALSE;
        samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
        samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        samplerInfo.minLod = 0.0f;
        samplerInfo.maxLod = static_cast<float>(mipLevels);
        samplerInfo.mipLodBias = 0.0f;

        if (vkCreateSampler(m_vkState._core.getDevice(), &samplerInfo, nullptr, &sampler) != VK_SUCCESS) {
            Utils::printLog(ERROR_PARAM, "failed to create texture sampler!");
        }

        m_samplers.try_emplace(mipLevels, sampler);

        return sampler;
    } else {
        return m_samplers[mipLevels];
    }
}

namespace {
// Averages a 2x2 block of the previous RGBA8 level to produce the next mip level (box filter).
// Pure CPU work so the resulting mip chain can be uploaded with plain buffer->image copies —
// no vkCmdBlitImage needed, so it works even on a queue with only VK_QUEUE_TRANSFER_BIT.
std::vector<uint8_t> generateNextMipLevel(const uint8_t* src, int srcWidth, int srcHeight, int& dstWidth, int& dstHeight) {
    dstWidth = std::max(1, srcWidth / 2);
    dstHeight = std::max(1, srcHeight / 2);
    std::vector<uint8_t> dst(static_cast<size_t>(dstWidth) * dstHeight * 4u);

    for (int y = 0; y < dstHeight; ++y) {
        const int srcY0 = std::min(2 * y, srcHeight - 1);
        const int srcY1 = std::min(2 * y + 1, srcHeight - 1);
        for (int x = 0; x < dstWidth; ++x) {
            const int srcX0 = std::min(2 * x, srcWidth - 1);
            const int srcX1 = std::min(2 * x + 1, srcWidth - 1);
            for (int c = 0; c < 4; ++c) {
                const int sum = src[(srcY0 * srcWidth + srcX0) * 4 + c] + src[(srcY0 * srcWidth + srcX1) * 4 + c] +
                                src[(srcY1 * srcWidth + srcX0) * 4 + c] + src[(srcY1 * srcWidth + srcX1) * 4 + c];
                dst[(y * dstWidth + x) * 4 + c] = static_cast<uint8_t>(sum / 4);
            }
        }
    }
    return dst;
}
}  // namespace

VkResult TextureFactory::loadImages(TextureFactory::Texture& outTexture, const std::vector<std::string>& textureFileNames,
                                    VkDevice device, VkPhysicalDevice physicalDevice, VkQueue queue, VkCommandPool cmdBufPool,
                                    bool is_miplevelsEnabling, bool is_flippingVertically, bool useTransferQueue) {
    using namespace Utils;
    const auto texturesAmount = textureFileNames.size();
    VkResult res;

    stbi_set_flip_vertically_on_load(is_flippingVertically);

    int texWidth, texHeight, texChannels;
    using datat_ptr = std::unique_ptr<stbi_uc, decltype(&stbi_image_free)>;
    std::vector<datat_ptr> textureData;
    textureData.reserve(texturesAmount);

    for (auto pStr : textureFileNames) {
        assert(pStr.data());
        /// STBI_rgb_alpha coerces to have ALPHA chanel for consistency with alphaless images
        stbi_uc* pixels = stbi_load(pStr.data(), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
        assert(pixels);
        textureData.emplace_back(pixels, stbi_image_free);
    }

    outTexture.width = texWidth;
    outTexture.height = texHeight;
    /// Note: calculating the number of levels in the mip chain:
    ///       std::log2 - how many times that dimension can be divided by 2
    ///       std::floor function handles cases where the largest dimension is not a power of 2
    ///       1 is added so that the original image has a mip level
    const uint32_t mipLevels =
        is_miplevelsEnabling ? static_cast<uint32_t>(std::floor(std::log2(MAX(texWidth, texHeight))) + 1.0) : 1U;
    outTexture.mipLevels = mipLevels;

    res = VulkanCreateImage(device, physicalDevice, texWidth, texHeight, IMAGE_FORMAT, VK_IMAGE_TILING_OPTIMAL,
                            VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, outTexture.m_textureImage, outTexture.m_textureImageMemory,
                            mipLevels, texturesAmount);

    VulkanTransitionImageLayout(device, queue, cmdBufPool, outTexture.m_textureImage, IMAGE_FORMAT, VK_IMAGE_LAYOUT_UNDEFINED,
                                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT, mipLevels, texturesAmount);

    if (!useTransferQueue) {
        // Graphics-capable queue (main thread, synchronous load): upload the base level only and let
        // the GPU generate the rest of the chain via vkCmdBlitImage, same as before.
        const VkDeviceSize layerSize = static_cast<VkDeviceSize>(texWidth) * texHeight * 4u;
        const VkDeviceSize imageSizeTotal = layerSize * texturesAmount;

        VkBuffer stagingBuffer;
        VkDeviceMemory stagingBufferMemory;
        VulkanCreateBuffer(device, physicalDevice, imageSizeTotal, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                           VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, stagingBuffer,
                           stagingBufferMemory);

        void* data;
        vkMapMemory(device, stagingBufferMemory, 0, imageSizeTotal, 0, &data);
        for (std::size_t i = 0; i < texturesAmount; ++i) {
            memcpy(static_cast<char*>(data) + layerSize * i, textureData[i].get(), static_cast<size_t>(layerSize));
        }
        vkUnmapMemory(device, stagingBufferMemory);

        VulkanCopyBufferToImage(device, queue, cmdBufPool, stagingBuffer, outTexture.m_textureImage,
                                static_cast<uint32_t>(texWidth), static_cast<uint32_t>(texHeight), texturesAmount);

        if (!is_miplevelsEnabling) {
            VulkanTransitionImageLayout(device, queue, cmdBufPool, outTexture.m_textureImage, IMAGE_FORMAT,
                                        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                        VK_IMAGE_ASPECT_COLOR_BIT, mipLevels, texturesAmount);
        } else {
            // Check if the format supports linear blitting before asking the GPU to do it.
            VkFormatProperties formatProperties{};
            vkGetPhysicalDeviceFormatProperties(physicalDevice, IMAGE_FORMAT, &formatProperties);
            if (!(formatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT)) {
                Utils::printLog(ERROR_PARAM, "texture image format does not support linear blitting!");
            }

            VulkanGenerateMipmaps(device, queue, cmdBufPool, outTexture.m_textureImage, IMAGE_FORMAT, texWidth, texHeight,
                                 mipLevels, texturesAmount);
        }

        vkDestroyBuffer(device, stagingBuffer, nullptr);
        vkFreeMemory(device, stagingBufferMemory, nullptr);

        return res;
    } else {
        // Pure DMA / transfer queue: vkCmdBlitImage isn't available there, so the whole mip chain is
        // generated on the CPU (box filter) instead and uploaded as plain per-level copies.
        // Unlike the Blit path (which only stages mip0 and lets the GPU derive the rest), we must
        // allocate the staging buffer for ALL mip levels up front since nothing generates them on GPU.
        std::vector<int> mipWidths(mipLevels), mipHeights(mipLevels);
        mipWidths[0] = texWidth;
        mipHeights[0] = texHeight;
        for (uint32_t level = 1u; level < mipLevels; ++level) {
            mipWidths[level] = std::max(1, mipWidths[level - 1] / 2);
            mipHeights[level] = std::max(1, mipHeights[level - 1] / 2);
        }

        // generatedMips[layer][level - 1] holds levels 1..mipLevels-1 (level 0 stays in textureData).
        std::vector<std::vector<std::vector<uint8_t>>> generatedMips(texturesAmount);
        for (std::size_t layer = 0u; layer < texturesAmount; ++layer) {
            generatedMips[layer].reserve(mipLevels - 1u);
            const uint8_t* prevLevel = textureData[layer].get();
            int prevWidth = texWidth;
            int prevHeight = texHeight;
            for (uint32_t level = 1u; level < mipLevels; ++level) {
                int dstWidth = 0;
                int dstHeight = 0;
                generatedMips[layer].push_back(generateNextMipLevel(prevLevel, prevWidth, prevHeight, dstWidth, dstHeight));
                prevLevel = generatedMips[layer].back().data();
                prevWidth = dstWidth;
                prevHeight = dstHeight;
            }
        }

        std::vector<VkDeviceSize> levelOffset(mipLevels);
        VkDeviceSize imageSizeTotal = 0u;
        for (uint32_t level = 0u; level < mipLevels; ++level) {
            levelOffset[level] = imageSizeTotal;
            const VkDeviceSize levelLayerSize = static_cast<VkDeviceSize>(mipWidths[level]) * mipHeights[level] * 4u;
            imageSizeTotal += levelLayerSize * texturesAmount;
        }

        VkBuffer stagingBuffer;
        VkDeviceMemory stagingBufferMemory;
        VulkanCreateBuffer(device, physicalDevice, imageSizeTotal, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                           VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, stagingBuffer,
                           stagingBufferMemory);

        std::vector<VkBufferImageCopy> regions(mipLevels);
        void* data;
        vkMapMemory(device, stagingBufferMemory, 0, imageSizeTotal, 0, &data);
        for (uint32_t level = 0u; level < mipLevels; ++level) {
            const VkDeviceSize levelLayerSize = static_cast<VkDeviceSize>(mipWidths[level]) * mipHeights[level] * 4u;
            for (std::size_t layer = 0u; layer < texturesAmount; ++layer) {
                const uint8_t* src = (level == 0u) ? textureData[layer].get() : generatedMips[layer][level - 1u].data();
                memcpy(static_cast<char*>(data) + levelOffset[level] + levelLayerSize * layer, src,
                      static_cast<size_t>(levelLayerSize));
            }

            VkBufferImageCopy& region = regions[level];
            region.bufferOffset = levelOffset[level];
            region.bufferRowLength = 0;
            region.bufferImageHeight = 0;
            region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            region.imageSubresource.mipLevel = level;
            region.imageSubresource.baseArrayLayer = 0;
            region.imageSubresource.layerCount = static_cast<uint32_t>(texturesAmount);
            region.imageOffset = {0, 0, 0};
            region.imageExtent = {static_cast<uint32_t>(mipWidths[level]), static_cast<uint32_t>(mipHeights[level]), 1u};
        }
        vkUnmapMemory(device, stagingBufferMemory);

        // One command buffer, one region per mip level — plain copies only, no blit, works on any
        // queue with VK_QUEUE_TRANSFER_BIT (including a pure DMA queue).
        VulkanCopyBufferToImageMipChain(device, queue, cmdBufPool, stagingBuffer, outTexture.m_textureImage, regions);
        // FRAGMENT_SHADER stage isn't valid on a transfer-only queue, unlike the graphics-queue path above.
        VulkanTransitionImageLayout(device, queue, cmdBufPool, outTexture.m_textureImage, IMAGE_FORMAT,
                                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                    VK_IMAGE_ASPECT_COLOR_BIT, mipLevels, texturesAmount,
                                    /*queueSupportsFragmentShaderStage=*/false);

        vkDestroyBuffer(device, stagingBuffer, nullptr);
        vkFreeMemory(device, stagingBufferMemory, nullptr);

        return res;
    }
}
