#pragma once

#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
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

    // useTransferQueue: upload via the transfer (DMA) queue/pool so this can be called from a
    // background loader thread; mip levels are generated on the CPU (box filter) and uploaded as
    // plain copies, so a pure DMA queue (no blit capability) works fine.
    std::weak_ptr<Texture> createCubeTexture(const std::array<std::string_view, 6>& textureFileNames,
                                             bool is_flippingVertically = true, bool useTransferQueue = false);
    std::weak_ptr<Texture> create2DTexture(std::string_view pTextureFileName, bool is_miplevelsEnabling = true,
                                           bool is_flippingVertically = true, bool useTransferQueue = false);
    // forceArrayView: request VK_IMAGE_VIEW_TYPE_2D_ARRAY even for a single file, for shaders using sampler2DArray (e.g. GPASS).
    std::weak_ptr<Texture> create2DArrayTexture(std::vector<std::string>&& textureFileNames, bool is_miplevelsEnabling = true,
                                                bool is_flippingVertically = true, bool forceArrayView = false,
                                                bool useTransferQueue = false);
    VkSampler getTextureSampler(uint32_t mipLevels);

private:
    /// <param name="is_flippingVertically"> keep it in 'true' by default since texture applies from top to bottom in
    /// Vulkan</param>
    // useTransferQueue selects the upload path: false -> GPU blit (vkCmdBlitImage) as before; true ->
    // CPU-generated mip chain (box filter) since a pure DMA queue can't run blit commands.
    VkResult loadImages(TextureFactory::Texture& outTexture, const std::vector<std::string>& textureFileNames, VkDevice device,
                        VkPhysicalDevice physicalDevice, VkQueue queue, VkCommandPool cmdBufPool,
                        bool is_miplevelsEnabling = true, bool is_flippingVertically = true, bool useTransferQueue = false);

private:
    const VulkanState& m_vkState;
    // Guards m_textures/m_samplers: Terrain/Skybox load on the main thread while other models may
    // load concurrently on a background loader thread using the transfer queue.
    // Recursive: create*Texture() holds the lock while calling getTextureSampler(), which also locks.
    std::recursive_mutex m_mutex;
    std::unordered_map<std::string, std::shared_ptr<Texture>, StringHash, std::equal_to<>> m_textures{};
    std::unordered_map<uint32_t, VkSampler> m_samplers{};
    VkPhysicalDeviceProperties m_properties{};
    std::function<void(TextureFactory::Texture* p)> mTextureDeleter{nullptr};
};