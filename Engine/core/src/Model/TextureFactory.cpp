#include "TextureFactory.h"
#include "Constants.h"
#include "Utils.h"

#include <assert.h>
#include <optional>
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
                                                                         bool is_flippingVertically) {
    auto id = std::string{textureFileNames[0]};

    if (m_textures.find(id) == m_textures.end()) {
        std::shared_ptr<TextureFactory::Texture> texture(new TextureFactory::Texture(), mTextureDeleter);

        std::vector<std::string> texturePaths{Utils::formPath(Constants::TEXTURES_DIR, textureFileNames[0]),
                                              Utils::formPath(Constants::TEXTURES_DIR, textureFileNames[1]),
                                              Utils::formPath(Constants::TEXTURES_DIR, textureFileNames[2]),
                                              Utils::formPath(Constants::TEXTURES_DIR, textureFileNames[3]),
                                              Utils::formPath(Constants::TEXTURES_DIR, textureFileNames[4]),
                                              Utils::formPath(Constants::TEXTURES_DIR, textureFileNames[5])};

        if (loadImages(*texture, texturePaths, m_vkState._core.getDevice(), m_vkState._core.getPhysDevice(), m_vkState._queue,
                       m_vkState._cmdBufPool, false, is_flippingVertically) != VK_SUCCESS) {
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
    } else {
        return m_textures[id];
    }
}

std::weak_ptr<TextureFactory::Texture> TextureFactory::create2DArrayTexture(std::vector<std::string>&& textureFileNames,
                                                                            bool is_miplevelsEnabling,
                                                                            bool is_flippingVertically) {
    std::vector<std::string> filePaths = std::move(textureFileNames);
    auto id = std::string{filePaths[0]};

    if (m_textures.find(id) == m_textures.end()) {
        std::shared_ptr<TextureFactory::Texture> texture(new TextureFactory::Texture(), mTextureDeleter);

        for (auto& filePath : filePaths) {
            filePath = Utils::formPath(Constants::TEXTURES_DIR, filePath);
        }

        if (loadImages(*texture, filePaths, m_vkState._core.getDevice(), m_vkState._core.getPhysDevice(), m_vkState._queue,
                       m_vkState._cmdBufPool, is_miplevelsEnabling, is_flippingVertically) != VK_SUCCESS) {
            Utils::printLog(ERROR_PARAM, "failed to create cubic texture image ");
        }
        if (Utils::VulkanCreateImageView(m_vkState._core.getDevice(), texture->m_textureImage, IMAGE_FORMAT,
                                         VK_IMAGE_ASPECT_COLOR_BIT, texture->m_textureImageView, texture->mipLevels,
                                         VK_IMAGE_VIEW_TYPE_2D_ARRAY, filePaths.size()) != VK_SUCCESS) {
            Utils::printLog(ERROR_PARAM, "failed to create 2DArray texture imageView ");
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
    if (m_textures.find(pTextureFileName.data()) == m_textures.end()) {
        std::shared_ptr<TextureFactory::Texture> texture(new TextureFactory::Texture(), mTextureDeleter);

        std::string texturePath = Utils::formPath(Constants::TEXTURES_DIR, pTextureFileName);

        if (loadImages(*texture, {texturePath}, m_vkState._core.getDevice(), m_vkState._core.getPhysDevice(), m_vkState._queue,
                       m_vkState._cmdBufPool, is_miplevelsEnabling, is_flippingVertically) != VK_SUCCESS) {
            Utils::printLog(ERROR_PARAM, "failed to create texture image ");
        }
        if (Utils::VulkanCreateImageView(m_vkState._core.getDevice(), texture->m_textureImage, IMAGE_FORMAT,
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

VkResult TextureFactory::loadImages(TextureFactory::Texture& outTexture, const std::vector<std::string>& textureFileNames,
                                    VkDevice device, VkPhysicalDevice physicalDevice, VkQueue queue, VkCommandPool cmdBufPool,
                                    bool is_miplevelsEnabling, bool is_flippingVertically) {
    using namespace Utils;
    const auto texturesAmount = textureFileNames.size();
    VkResult res;

    stbi_set_flip_vertically_on_load(is_flippingVertically);

    int texWidth, texHeight, texChannels;
    VkDeviceSize imageSizeTotal = 0u;
    using datat_ptr = std::unique_ptr<stbi_uc, decltype(&stbi_image_free)>;
    std::vector<datat_ptr> textureData;
    textureData.reserve(texturesAmount);

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
    /// Note: calculating the number of levels in the mip chain:
    ///       std::log2 - how many times that dimension can be divided by 2
    ///       std::floor function handles cases where the largest dimension is not a power of 2
    ///       1 is added so that the original image has a mip level
    const uint32_t mipLevels =
        is_miplevelsEnabling ? static_cast<uint32_t>(std::floor(std::log2(MAX(texWidth, texHeight))) + 1.0) : 1U;
    outTexture.mipLevels = mipLevels;

    const VkDeviceSize layerSize = imageSizeTotal / texturesAmount;  // This is just the size of each layer.

    VkBuffer stagingBuffer;
    VkDeviceMemory stagingBufferMemory;
    VulkanCreateBuffer(device, physicalDevice, imageSizeTotal, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                       VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, stagingBuffer,
                       stagingBufferMemory);

    void* data;
    vkMapMemory(device, stagingBufferMemory, 0, imageSizeTotal, 0, &data);
    for (std::size_t i = 0; i < texturesAmount; ++i) {
        memcpy((char*)data + (layerSize * i), textureData[i].get(), static_cast<size_t>(layerSize));
    }
    vkUnmapMemory(device, stagingBufferMemory);

    res = VulkanCreateImage(device, physicalDevice, texWidth, texHeight, IMAGE_FORMAT, VK_IMAGE_TILING_OPTIMAL,
                            VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, outTexture.m_textureImage, outTexture.m_textureImageMemory,
                            mipLevels, texturesAmount);

    VulkanTransitionImageLayout(device, queue, cmdBufPool, outTexture.m_textureImage, IMAGE_FORMAT, VK_IMAGE_LAYOUT_UNDEFINED,
                                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT, mipLevels, texturesAmount);
    VulkanCopyBufferToImage(device, queue, cmdBufPool, stagingBuffer, outTexture.m_textureImage, static_cast<uint32_t>(texWidth),
                            static_cast<uint32_t>(texHeight), texturesAmount);

    // Check if image format supports linear blitting
    static std::optional<VkFormatProperties> formatProperties;
    if (!formatProperties) {
        formatProperties = VkFormatProperties{};
        vkGetPhysicalDeviceFormatProperties(physicalDevice, IMAGE_FORMAT, &formatProperties.value());
    }

    if (!(formatProperties.value().optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT)) {
        Utils::printLog(ERROR_PARAM, "texture image format does not support linear blitting!");
    }

    if (!is_miplevelsEnabling) {
        VulkanTransitionImageLayout(device, queue, cmdBufPool, outTexture.m_textureImage, IMAGE_FORMAT,
                                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                    VK_IMAGE_ASPECT_COLOR_BIT, mipLevels, texturesAmount);
    } else {
        VulkanGenerateMipmaps(device, queue, cmdBufPool, outTexture.m_textureImage, IMAGE_FORMAT, texWidth, texHeight, mipLevels,
                              texturesAmount);
    }

    vkDestroyBuffer(device, stagingBuffer, nullptr);
    vkFreeMemory(device, stagingBufferMemory, nullptr);

    return res;
}
