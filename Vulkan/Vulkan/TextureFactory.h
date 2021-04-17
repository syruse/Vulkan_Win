#pragma once

#include <string>
#include "vulkan/vulkan.h"
#include <unordered_map>
#include <memory>

class TextureFactory
{
public:

    static std::string TEXTURES_DIR;

    struct Texture
    {
        VkImage m_textureImage = nullptr;
        VkDeviceMemory m_textureImageMemory = nullptr;
        VkImageView m_textureImageView = nullptr;
        uint32_t mipLevels;
    };

    static TextureFactory& init(VkDevice device, VkPhysicalDevice physicalDevice, VkCommandPool cmdBufPool, VkQueue queue)
    {
        static TextureFactory texFactory(device, physicalDevice, cmdBufPool, queue);
        return texFactory;
    }

    ~TextureFactory();

    std::shared_ptr<Texture> create2DTexture(std::string_view pTextureFileName, bool is_miplevelsEnabling = true, bool is_flippingVertically = true);

private:
    TextureFactory(VkDevice device, VkPhysicalDevice physicalDevice, VkCommandPool cmdBufPool, VkQueue queue);
    void texture_deleter(TextureFactory::Texture *p);
    VkSampler getTextureSampler(uint32_t mipLevels);
private:
    std::unordered_map<std::string, std::shared_ptr<Texture>> m_textures{};
    std::unordered_map<uint32_t, VkSampler> m_samplers{};
    VkPhysicalDeviceProperties m_properties{};
    VkDevice m_device = nullptr;
    VkPhysicalDevice m_physicalDevice = nullptr;
    VkCommandPool m_cmdBufPool = nullptr;
    VkQueue m_queue = nullptr;
};