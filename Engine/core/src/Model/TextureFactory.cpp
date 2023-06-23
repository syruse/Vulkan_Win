#include "TextureFactory.h"
#include "Constants.h"
#include "Utils.h"

#include <assert.h>
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

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
                                                                         bool is_flippingVertically) {
    auto id = std::string{textureFileNames[0]};

    if (m_textures.count(id) == 0) {
        std::shared_ptr<TextureFactory::Texture> texture(new TextureFactory::Texture(), mTextureDeleter);

        std::array<std::string, 6> _textureFileNames{Utils::formPath(Constants::TEXTURES_DIR, textureFileNames[0]),
                                                     Utils::formPath(Constants::TEXTURES_DIR, textureFileNames[1]),
                                                     Utils::formPath(Constants::TEXTURES_DIR, textureFileNames[2]),
                                                     Utils::formPath(Constants::TEXTURES_DIR, textureFileNames[3]),
                                                     Utils::formPath(Constants::TEXTURES_DIR, textureFileNames[4]),
                                                     Utils::formPath(Constants::TEXTURES_DIR, textureFileNames[5])};

        if (VulkanCreateCubeTextureImage(*texture, _textureFileNames, m_vkState._core.getDevice(),
                                         m_vkState._core.getPhysDevice(), m_vkState._queue,
                                         m_vkState._cmdBufPool) != VK_SUCCESS) {
            Utils::printLog(ERROR_PARAM, "failed to create cubic texture image ");
        }
        if (Utils::VulkanCreateImageView(m_vkState._core.getDevice(), texture->m_textureImage, VK_FORMAT_R8G8B8A8_SRGB,
                                         VK_IMAGE_ASPECT_COLOR_BIT, texture->m_textureImageView, 1U, VK_IMAGE_VIEW_TYPE_CUBE,
                                         6U) != VK_SUCCESS) {
            Utils::printLog(ERROR_PARAM, "failed to create cubic texture imageView ");
        }

        /// Note: creation of sampler in advance
        getTextureSampler(texture->mipLevels);
        m_textures.try_emplace(id, texture);

        return texture;
    } else {
        return m_textures[id];
    }
}

std::weak_ptr<TextureFactory::Texture> TextureFactory::create2DTexture(std::string_view pTextureFileName,
                                                                       bool is_miplevelsEnabling, bool is_flippingVertically) {
    if (m_textures.count(pTextureFileName.data()) == 0) {
        std::shared_ptr<TextureFactory::Texture> texture(new TextureFactory::Texture(), mTextureDeleter);

        std::string texturePath = Utils::formPath(Constants::TEXTURES_DIR, pTextureFileName);

        if (VulkanCreateTextureImage(*texture, texturePath.c_str(), m_vkState._core.getDevice(), m_vkState._core.getPhysDevice(),
                                     m_vkState._queue, m_vkState._cmdBufPool) != VK_SUCCESS) {
            Utils::printLog(ERROR_PARAM, "failed to create texture image ");
        }
        if (Utils::VulkanCreateImageView(m_vkState._core.getDevice(), texture->m_textureImage, VK_FORMAT_R8G8B8A8_SRGB,
                                         VK_IMAGE_ASPECT_COLOR_BIT, texture->m_textureImageView,
                                         texture->mipLevels) != VK_SUCCESS) {
            Utils::printLog(ERROR_PARAM, "failed to create texture imageView ");
        }

        /// Note: creation of sampler in advance
        getTextureSampler(texture->mipLevels);
        m_textures.try_emplace(std::string{pTextureFileName}, texture);

        return texture;
    } else {
        return m_textures[std::string{pTextureFileName}];
    }
}

VkSampler TextureFactory::getTextureSampler(uint32_t mipLevels) {
    if (m_samplers.count(mipLevels) == 0) {
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

VkResult TextureFactory::VulkanCreateTextureImage(TextureFactory::Texture& outTexture, std::string_view pTextureFileName,
                                                  VkDevice device, VkPhysicalDevice physicalDevice, VkQueue queue,
                                                  VkCommandPool cmdBufPool, bool is_miplevelsEnabling,
                                                  bool is_flippingVertically) {
    using namespace Utils;
    VkResult res;

    stbi_set_flip_vertically_on_load(is_flippingVertically);

    int texWidth, texHeight, texChannels;
    VkFormat imageFormat = VK_FORMAT_R8G8B8A8_SRGB;
    /// STBI_rgb_alpha coerces to have ALPHA chanel for consistency with alphaless images
    stbi_uc* pixels = stbi_load(pTextureFileName.data(), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
    VkDeviceSize imageSize = static_cast<VkDeviceSize>(texWidth * texHeight * 4LL);

    outTexture.width = texWidth;
    outTexture.height = texHeight;

    /// Note: calculating the number of levels in the mip chain:
    ///       std::log2 - how many times that dimension can be divided by 2
    ///       std::floor function handles cases where the largest dimension is not a power of 2
    ///       1 is added so that the original image has a mip level
    uint32_t mipLevels = is_miplevelsEnabling ? static_cast<uint32_t>(std::floor(std::log2(MAX(texWidth, texHeight))) + 1.0) : 1U;
    outTexture.mipLevels = mipLevels;

    if (!pixels) {
        Utils::printLog(ERROR_PARAM, pTextureFileName.data(), "failed to load texture image!");
    }

    VkBuffer stagingBuffer;
    VkDeviceMemory stagingBufferMemory;
    VulkanCreateBuffer(device, physicalDevice, imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                       VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, stagingBuffer,
                       stagingBufferMemory);

    void* data;
    vkMapMemory(device, stagingBufferMemory, 0, imageSize, 0, &data);
    memcpy(data, pixels, static_cast<size_t>(imageSize));
    vkUnmapMemory(device, stagingBufferMemory);

    stbi_image_free(pixels);

    res = VulkanCreateImage(device, physicalDevice, texWidth, texHeight, imageFormat, VK_IMAGE_TILING_OPTIMAL,
                            VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, outTexture.m_textureImage, outTexture.m_textureImageMemory,
                            mipLevels);

    VulkanTransitionImageLayout(device, queue, cmdBufPool, outTexture.m_textureImage, imageFormat, VK_IMAGE_LAYOUT_UNDEFINED,
                                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT, mipLevels);
    VulkanCopyBufferToImage(device, queue, cmdBufPool, stagingBuffer, outTexture.m_textureImage, static_cast<uint32_t>(texWidth),
                            static_cast<uint32_t>(texHeight));

    // Check if image format supports linear blitting
    VkFormatProperties formatProperties;
    vkGetPhysicalDeviceFormatProperties(physicalDevice, imageFormat, &formatProperties);  /// TO FIX

    if (!(formatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT)) {
        Utils::printLog(ERROR_PARAM, "texture image format does not support linear blitting!");
    }

    if (!is_miplevelsEnabling) {
        VulkanTransitionImageLayout(device, queue, cmdBufPool, outTexture.m_textureImage, imageFormat,
                                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    } else {
        VulkanGenerateMipmaps(device, queue, cmdBufPool, outTexture.m_textureImage, imageFormat, texWidth, texHeight, mipLevels);
    }

    vkDestroyBuffer(device, stagingBuffer, nullptr);
    vkFreeMemory(device, stagingBufferMemory, nullptr);

    return res;
}

VkResult TextureFactory::VulkanCreateCubeTextureImage(TextureFactory::Texture& outTexture,
                                                      const std::array<std::string, 6>& textureFileNames, VkDevice device,
                                                      VkPhysicalDevice physicalDevice, VkQueue queue, VkCommandPool cmdBufPool,
                                                      bool is_flippingVertically) {
    using namespace Utils;
    VkResult res;

    stbi_set_flip_vertically_on_load(is_flippingVertically);

    int texWidth, texHeight, texChannels;
    const VkFormat imageFormat = VK_FORMAT_R8G8B8A8_SRGB;
    VkDeviceSize imageSizeTotal = 0u;
    using datat_ptr = std::unique_ptr<stbi_uc, decltype(&stbi_image_free)>;
    std::vector<datat_ptr> textureData;
    textureData.reserve(6);

    for (auto pStr : textureFileNames) {
        assert(pStr.data());
        /// STBI_rgb_alpha coerces to have ALPHA chanel for consistency with alphaless images
        stbi_uc* pixels = stbi_load(pStr.data(), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
        assert(pixels);
        textureData.emplace_back(pixels, stbi_image_free);
        imageSizeTotal += static_cast<VkDeviceSize>(texWidth * texHeight * 4LL);
    }

    outTexture.width = texWidth;
    outTexture.height = texHeight;
    outTexture.mipLevels = 1u;

    const VkDeviceSize layerSize = imageSizeTotal / 6;  // This is just the size of each layer.

    VkBuffer stagingBuffer;
    VkDeviceMemory stagingBufferMemory;
    VulkanCreateBuffer(device, physicalDevice, imageSizeTotal, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                       VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, stagingBuffer,
                       stagingBufferMemory);

    void* data;
    vkMapMemory(device, stagingBufferMemory, 0, imageSizeTotal, 0, &data);
    for (std::size_t i = 0; i < 6u; ++i) {
        memcpy((char*)data + (layerSize * i), textureData[i].get(), static_cast<size_t>(layerSize));
    }
    vkUnmapMemory(device, stagingBufferMemory);

    res = VulkanCreateImage(device, physicalDevice, texWidth, texHeight, imageFormat, VK_IMAGE_TILING_OPTIMAL,
                            VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, outTexture.m_textureImage, outTexture.m_textureImageMemory, 1u,
                            6u);

    VulkanTransitionImageLayout(device, queue, cmdBufPool, outTexture.m_textureImage, imageFormat, VK_IMAGE_LAYOUT_UNDEFINED,
                                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT, 1u, 6u);
    VulkanCopyBufferToImage(device, queue, cmdBufPool, stagingBuffer, outTexture.m_textureImage, static_cast<uint32_t>(texWidth),
                            static_cast<uint32_t>(texHeight), 6u);

    VulkanTransitionImageLayout(device, queue, cmdBufPool, outTexture.m_textureImage, imageFormat,
                                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                VK_IMAGE_ASPECT_COLOR_BIT, 1u, 6u);

    vkDestroyBuffer(device, stagingBuffer, nullptr);
    vkFreeMemory(device, stagingBufferMemory, nullptr);

    return res;
}
