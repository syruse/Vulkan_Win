
#include "TextureFactory.h"
#include "Utils.h"
#include <assert.h>

std::string TextureFactory::TEXTURES_DIR = "textures";

TextureFactory::TextureFactory(VkDevice device, VkPhysicalDevice physicalDevice, VkCommandPool cmdBufPool, VkQueue queue) noexcept(true)
    : m_device(device)
    , m_physicalDevice(physicalDevice)
    , m_cmdBufPool(cmdBufPool)
    , m_queue(queue)
{
    assert(m_device);
    assert(m_physicalDevice);
    assert(m_cmdBufPool);
    assert(m_queue);

    mTextureDeleter = [this](TextureFactory::Texture *p) {
        Utils::printLog(INFO_PARAM, "texture resources removal");
        vkDestroyImageView(m_device, p->m_textureImageView, nullptr);
        vkDestroyImage(m_device, p->m_textureImage, nullptr);
        vkFreeMemory(m_device, p->m_textureImageMemory, nullptr);
        delete p;
    };

    vkGetPhysicalDeviceProperties(m_physicalDevice, &m_properties);
    Utils::printLog(INFO_PARAM, "maxSamplerAnisotrop: ", m_properties.limits.maxSamplerAnisotropy);
}

TextureFactory::~TextureFactory()
{
    // Wait until no actions being run on device before destroying
    vkDeviceWaitIdle(m_device);
    for( const auto& [key, value] : m_samplers ) 
    {
        Utils::printLog(INFO_PARAM, "sampler removal with miplevels: ", key);
        vkDestroySampler(m_device, value, nullptr);
    }
}

std::weak_ptr<TextureFactory::Texture> TextureFactory::createCubeTexture(const std::array<std::string_view, 6>& textureFileNames, bool is_flippingVertically)
{
    auto id = textureFileNames[0].data();
    uint32_t mipLevels = 1U;

    if (m_textures.count(id) == 0)
    {
        std::shared_ptr<TextureFactory::Texture> texture(new TextureFactory::Texture(), mTextureDeleter);

        std::array<std::string, 6> _textureFileNames
        {
            Utils::formPath(TEXTURES_DIR, textureFileNames[0]),
            Utils::formPath(TEXTURES_DIR, textureFileNames[1]),
            Utils::formPath(TEXTURES_DIR, textureFileNames[2]),
            Utils::formPath(TEXTURES_DIR, textureFileNames[3]),
            Utils::formPath(TEXTURES_DIR, textureFileNames[4]),
            Utils::formPath(TEXTURES_DIR, textureFileNames[5])
        };

        if (Utils::VulkanCreateCubeTextureImage(m_device, m_physicalDevice, m_queue, m_cmdBufPool, _textureFileNames, texture->m_textureImage, texture->m_textureImageMemory) != VK_SUCCESS)
        {
            Utils::printLog(ERROR_PARAM, "failed to create cubic texture image ");
        }
        if (Utils::VulkanCreateImageView(m_device, texture->m_textureImage, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_ASPECT_COLOR_BIT, texture->m_textureImageView, 1U, VK_IMAGE_VIEW_TYPE_CUBE, 6U) != VK_SUCCESS)
        {
            Utils::printLog(ERROR_PARAM, "failed to create cubic texture imageView ");
        }

        /// Note: creation of sampler in advance
        getTextureSampler(mipLevels);
        texture->mipLevels = mipLevels;
        m_textures.try_emplace(id, texture);

        return texture;
    }
    else
    {
        return m_textures[id];
    }
}

std::weak_ptr<TextureFactory::Texture> TextureFactory::create2DTexture(std::string_view pTextureFileName, bool is_miplevelsEnabling, bool is_flippingVertically)
{
    if(m_textures.count(pTextureFileName.data()) == 0)
    {
        std::shared_ptr<TextureFactory::Texture> texture(new TextureFactory::Texture(), mTextureDeleter);

        uint32_t mipLevels = 0U;

        std::string texturePath;
        Utils::formPath(TEXTURES_DIR, pTextureFileName, texturePath);

        if(Utils::VulkanCreateTextureImage(m_device, m_physicalDevice, m_queue, m_cmdBufPool, texturePath.c_str(), texture->m_textureImage, texture->m_textureImageMemory, mipLevels) != VK_SUCCESS)
        {
            Utils::printLog(ERROR_PARAM, "failed to create texture image ");
        }
        if(Utils::VulkanCreateImageView(m_device, texture->m_textureImage, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_ASPECT_COLOR_BIT, texture->m_textureImageView, mipLevels) != VK_SUCCESS)
        {
            Utils::printLog(ERROR_PARAM, "failed to create texture imageView ");
        }

        /// Note: creation of sampler in advance
        getTextureSampler(mipLevels);
        texture->mipLevels = mipLevels;
        m_textures.try_emplace(pTextureFileName.data(), texture);

        return texture;
    }
    else
    {
        return m_textures[pTextureFileName.data()];
    }
}

VkSampler TextureFactory::getTextureSampler(uint32_t mipLevels)
{
    if (m_samplers.count(mipLevels) == 0)
    {
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
        samplerInfo.unnormalizedCoordinates = VK_FALSE; /// -> [0: 1]
        samplerInfo.compareEnable = VK_FALSE;
        samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
        samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        samplerInfo.minLod = 0.0f;
        samplerInfo.maxLod = static_cast<float>(mipLevels);
        samplerInfo.mipLodBias = 0.0f;

        if (vkCreateSampler(m_device, &samplerInfo, nullptr, &sampler) != VK_SUCCESS)
        {
            Utils::printLog(ERROR_PARAM, "failed to create texture sampler!");
        }

        m_samplers.try_emplace(mipLevels, sampler);
        
        return sampler;
    }
    else
    {
        return m_samplers[mipLevels];
    }
}