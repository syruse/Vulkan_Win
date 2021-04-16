
#include "TextureFactory.h"
#include "Utils.h"
#include <assert.h>

std::string TextureFactory::TEXTURES_DIR = "textures";

TextureFactory::TextureFactory(VkDevice device, VkPhysicalDevice physicalDevice, VkCommandPool cmdBufPool, VkQueue queue)
    : m_device(device)
    , m_physicalDevice(physicalDevice)
    , m_cmdBufPool(cmdBufPool)
    , m_queue(queue)
{
    assert(m_device);
    assert(m_physicalDevice);
    assert(m_cmdBufPool);
    assert(m_queue);
}

TextureFactory::~TextureFactory()
{
            vkDestroySampler(m_core.getDevice(), m_textureSampler, nullptr);
            vkDestroyImageView(m_core.getDevice(), m_textureImageView, nullptr);
            vkDestroyImage(m_core.getDevice(), m_textureImage, nullptr);
            vkFreeMemory(m_core.getDevice(), m_textureImageMemory, nullptr);
}

TextureFactory::Texture TextureFactory::create2DTexture(std::string_view pTextureFileName, bool is_miplevelsEnabling, bool is_flippingVertically)
{
    TextureFactory::Texture texture{};

    if(m_textures.count(pTextureFileName.data()) == 0)
    {
        m_mipLevels = Utils::VulkanCreateTextureImage(m_core.getDevice(), m_core.getPhysDevice(), m_queue, m_cmdBufPool, TEXTURE_FILE_NAME, m_textureImage, m_textureImageMemory);

        	if (Utils::VulkanCreateImageView(m_core.getDevice(), m_textureImage, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_ASPECT_COLOR_BIT, m_textureImageView, m_mipLevels) != VK_SUCCESS) {
		Utils::printLog(ERROR_PARAM, "failed to create texture image view!");
	}
    }
    else
    {
        texture = m_textures[pTextureFileName.data()];
    }

    return texture;
}

void VulkanRenderer::createTextureSampler()
{
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(m_core.getPhysDevice(), &properties);

    Utils::printLog(INFO_PARAM, "maxSamplerAnisotrop: ", properties.limits.maxSamplerAnisotropy);

    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.anisotropyEnable = (properties.limits.maxSamplerAnisotropy < 1 ? VK_FALSE : VK_TRUE);
    samplerInfo.maxAnisotropy = properties.limits.maxSamplerAnisotropy;
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE; /// -> [0: 1]
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = static_cast<float>(m_mipLevels);
    samplerInfo.mipLodBias = 0.0f;

    if (vkCreateSampler(m_core.getDevice(), &samplerInfo, nullptr, &m_textureSampler) != VK_SUCCESS) {
        Utils::printLog(ERROR_PARAM, "failed to create texture sampler!");
    }
}