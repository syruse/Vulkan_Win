
#include "TextureFactory.h"
#include "Constants.h"
#include "Utils.h"
#include <assert.h>

TextureFactory::TextureFactory(const VulkanState& vulkanState) noexcept(true)
    : m_vkState(vulkanState)
{
    mTextureDeleter = [this](TextureFactory::Texture *p) {
        auto p_devide = m_vkState._core.getDevice();
        assert(p_devide);
        Utils::printLog(INFO_PARAM, "texture resources removal");
        vkDestroyImageView(p_devide, p->m_textureImageView, nullptr);
        vkDestroyImage(p_devide, p->m_textureImage, nullptr);
        vkFreeMemory(p_devide, p->m_textureImageMemory, nullptr);
        delete p;
    };
}

TextureFactory::~TextureFactory()
{
    // Wait until no actions being run on device before destroying
    std::ignore = vkDeviceWaitIdle(m_vkState._core.getDevice());
    for( const auto& [key, value] : m_samplers ) 
    {
        Utils::printLog(INFO_PARAM, "sampler removal with miplevels: ", key);
        vkDestroySampler(m_vkState._core.getDevice(), value, nullptr);
    }
}

void TextureFactory::init() {
    vkGetPhysicalDeviceProperties(m_vkState._core.getPhysDevice(), &m_properties);
    Utils::printLog(INFO_PARAM, "maxSamplerAnisotrop: ", m_properties.limits.maxSamplerAnisotropy);
}

std::weak_ptr<TextureFactory::Texture> TextureFactory::createCubeTexture(const std::array<std::string_view, 6>& textureFileNames, bool is_flippingVertically)
{
    auto id = std::string{ textureFileNames[0] };
    uint32_t mipLevels = 1U;

    if (m_textures.count(id) == 0)
    {
        std::shared_ptr<TextureFactory::Texture> texture(new TextureFactory::Texture(), mTextureDeleter);

        std::array<std::string, 6> _textureFileNames
        {
            Utils::formPath(Constants::TEXTURES_DIR, textureFileNames[0]),
            Utils::formPath(Constants::TEXTURES_DIR, textureFileNames[1]),
            Utils::formPath(Constants::TEXTURES_DIR, textureFileNames[2]),
            Utils::formPath(Constants::TEXTURES_DIR, textureFileNames[3]),
            Utils::formPath(Constants::TEXTURES_DIR, textureFileNames[4]),
            Utils::formPath(Constants::TEXTURES_DIR, textureFileNames[5])
        };

        if (Utils::VulkanCreateCubeTextureImage(m_vkState._core.getDevice(), m_vkState._core.getPhysDevice(), m_vkState._queue, m_vkState._cmdBufPool, 
            _textureFileNames, texture->m_textureImage, texture->m_textureImageMemory) != VK_SUCCESS)
        {
            Utils::printLog(ERROR_PARAM, "failed to create cubic texture image ");
        }
        if (Utils::VulkanCreateImageView(m_vkState._core.getDevice(), texture->m_textureImage, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_ASPECT_COLOR_BIT, 
            texture->m_textureImageView, 1U, VK_IMAGE_VIEW_TYPE_CUBE, 6U) != VK_SUCCESS)
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
	if (m_textures.count(pTextureFileName.data()) == 0)
	{
		std::shared_ptr<TextureFactory::Texture> texture(new TextureFactory::Texture(), mTextureDeleter);

		uint32_t mipLevels = 0U;

		std::string texturePath = Utils::formPath(Constants::TEXTURES_DIR, pTextureFileName);

		if (Utils::VulkanCreateTextureImage(m_vkState._core.getDevice(), m_vkState._core.getPhysDevice(), m_vkState._queue, m_vkState._cmdBufPool,
			texturePath.c_str(), texture->m_textureImage, texture->m_textureImageMemory, mipLevels) != VK_SUCCESS)
		{
			Utils::printLog(ERROR_PARAM, "failed to create texture image ");
		}
		if (Utils::VulkanCreateImageView(m_vkState._core.getDevice(), texture->m_textureImage, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_ASPECT_COLOR_BIT,
			texture->m_textureImageView, mipLevels) != VK_SUCCESS)
		{
			Utils::printLog(ERROR_PARAM, "failed to create texture imageView ");
		}

		/// Note: creation of sampler in advance
		getTextureSampler(mipLevels);
		texture->mipLevels = mipLevels;
		m_textures.try_emplace(std::string{ pTextureFileName }, texture);

		return texture;
	}
	else
	{
		return m_textures[std::string{ pTextureFileName }];
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

        if (vkCreateSampler(m_vkState._core.getDevice(), &samplerInfo, nullptr, &sampler) != VK_SUCCESS)
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
