#pragma once

#include "VulkanState.h"
#include <functional>
#include <string>
#include <memory>
#include <unordered_map>

class TextureFactory
{
public:
    struct Texture
    {
        VkImage m_textureImage{ nullptr };
        VkDeviceMemory m_textureImageMemory{ nullptr };
        VkImageView m_textureImageView{ nullptr };
        uint32_t mipLevels{ 0u };
    };

    TextureFactory(const VulkanState& vulkanState) noexcept(true);

    void init();

    ~TextureFactory();

    std::weak_ptr<Texture> createCubeTexture(const std::array<std::string_view, 6>& textureFileNames, bool is_flippingVertically = true);
    std::weak_ptr<Texture> create2DTexture(std::string_view pTextureFileName, bool is_miplevelsEnabling = true, bool is_flippingVertically = true);
    VkSampler getTextureSampler(uint32_t mipLevels);

private:
    const VulkanState& m_vkState;
    std::unordered_map<std::string, std::shared_ptr<Texture>> m_textures{};
    std::unordered_map<uint32_t, VkSampler> m_samplers{};
    VkPhysicalDeviceProperties m_properties{};
    std::function<void(TextureFactory::Texture* p)> mTextureDeleter{ nullptr };
};