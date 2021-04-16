#pragma once

#include <string>
#include "vulkan/vulkan.h"
#include <unordered_map>

class TextureFactory
{
public:

    static std::string TEXTURES_DIR;

    struct Texture
    {
        VkImage m_textureImage = nullptr;
        VkDeviceMemory m_textureImageMemory = nullptr;
        VkImageView m_textureImageView = nullptr;
        VkSampler m_textureSampler = nullptr;
    };

    ~TextureFactory();

    static TextureFactory& init(VkDevice device, VkPhysicalDevice physicalDevice, VkCommandPool cmdBufPool, VkQueue queue)
    {
        static TextureFactory texFactory(device, physicalDevice, cmdBufPool, queue);
        return texFactory;
    }

    Texture& create2DTexture(std::string_view pTextureFileName, bool is_miplevelsEnabling = true, bool is_flippingVertically = true);

private:
    TextureFactory(VkDevice device, VkPhysicalDevice physicalDevice, VkCommandPool cmdBufPool, VkQueue queue);

private:
    std::unordered_map<std::string, Texture> m_textures{};
    VkDevice m_device = nullptr;
    VkPhysicalDevice m_physicalDevice = nullptr;
    VkCommandPool m_cmdBufPool = nullptr;
    VkQueue m_queue = nullptr;
};