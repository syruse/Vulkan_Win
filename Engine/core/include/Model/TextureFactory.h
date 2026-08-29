#pragma once

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include "VulkanState.h"

// Transparent hasher so lookups by std::string_view/const char* don't allocate a temporary std::string.
struct StringHash {
    using is_transparent = void;
    size_t operator()(std::string_view sv) const noexcept { return std::hash<std::string_view>{}(sv); }
};

class TextureFactory {
public:
    struct Texture {
        VkImage m_textureImage{nullptr};
        VkDeviceMemory m_textureImageMemory{nullptr};
        VkImageView m_textureImageView{nullptr};
        uint32_t mipLevels{0u};
        uint32_t width{0u};
        uint32_t height{0u};
    };

    TextureFactory(const VulkanState& vulkanState) noexcept(true);

    void init();

    ~TextureFactory();

    std::weak_ptr<Texture> createCubeTexture(const std::array<std::string_view, 6>& textureFileNames,
                                             bool is_flippingVertically = true);
    std::weak_ptr<Texture> create2DTexture(std::string_view pTextureFileName, bool is_miplevelsEnabling = true,
                                           bool is_flippingVertically = true);
    // forceArrayView: request VK_IMAGE_VIEW_TYPE_2D_ARRAY even for a single file, for shaders using sampler2DArray (e.g. GPASS).
    std::weak_ptr<Texture> create2DArrayTexture(std::vector<std::string>&& textureFileNames, bool is_miplevelsEnabling = true,
                                                bool is_flippingVertically = true, bool forceArrayView = false);
    VkSampler getTextureSampler(uint32_t mipLevels);

private:
    /// <param name="is_flippingVertically"> keep it in 'true' by default since texture applies from top to bottom in
    /// Vulkan</param>
    VkResult loadImages(TextureFactory::Texture& outTexture, const std::vector<std::string>& textureFileNames, VkDevice device,
                        VkPhysicalDevice physicalDevice, VkQueue queue, VkCommandPool cmdBufPool,
                        bool is_miplevelsEnabling = true, bool is_flippingVertically = true);

private:
    const VulkanState& m_vkState;
    std::unordered_map<std::string, std::shared_ptr<Texture>, StringHash, std::equal_to<>> m_textures{};
    std::unordered_map<uint32_t, VkSampler> m_samplers{};
    VkPhysicalDeviceProperties m_properties{};
    std::function<void(TextureFactory::Texture* p)> mTextureDeleter{nullptr};
};