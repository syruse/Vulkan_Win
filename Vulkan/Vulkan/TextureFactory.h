#pragma once

#include <string>
#include "vulkan/vulkan.h"
#include <unordered_map>
#include <memory>
#include <functional>

class TextureFactory
{
public:

    static std::string TEXTURES_DIR;

    struct Texture
    {
        VkImage m_textureImage = nullptr;
        VkDeviceMemory m_textureImageMemory = nullptr;
        VkImageView m_textureImageView = nullptr;
        uint32_t mipLevels = 0u;
    };

    static TextureFactory& init(VkDevice device, VkPhysicalDevice physicalDevice, VkCommandPool cmdBufPool, VkQueue queue)
    {
        static TextureFactory texFactory(device, physicalDevice, cmdBufPool, queue);
        return texFactory;
    }

    ~TextureFactory();

    std::shared_ptr<Texture> create2DTexture(std::string_view pTextureFileName, bool is_miplevelsEnabling = true, bool is_flippingVertically = true);
    VkSampler getTextureSampler(uint32_t mipLevels);

private:
    TextureFactory(VkDevice device, VkPhysicalDevice physicalDevice, VkCommandPool cmdBufPool, VkQueue queue);
private:
    std::unordered_map<std::string, std::shared_ptr<Texture>> m_textures{};
    std::unordered_map<uint32_t, VkSampler> m_samplers{};
    VkPhysicalDeviceProperties m_properties{};
    VkDevice m_device = nullptr;
    VkPhysicalDevice m_physicalDevice = nullptr;
    VkCommandPool m_cmdBufPool = nullptr;
    VkQueue m_queue = nullptr;
    std::function<void(TextureFactory::Texture *p)> mTextureDeleter = nullptr;
};