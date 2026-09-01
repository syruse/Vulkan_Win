#pragma once

#include <atomic>
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
    // background loader thread; mip-chain generation is automatically skipped if that queue can't blit.
    std::weak_ptr<Texture> createCubeTexture(const std::array<std::string_view, 6>& textureFileNames,
                                             bool is_flippingVertically = true, bool useTransferQueue = false);
    std::weak_ptr<Texture> create2DTexture(std::string_view pTextureFileName, bool is_miplevelsEnabling = true,
                                           bool is_flippingVertically = true, bool useTransferQueue = false);
    // forceArrayView: request VK_IMAGE_VIEW_TYPE_2D_ARRAY even for a single file, for shaders using sampler2DArray (e.g. GPASS).
    std::weak_ptr<Texture> create2DArrayTexture(std::vector<std::string>&& textureFileNames, bool is_miplevelsEnabling = true,
                                                bool is_flippingVertically = true, bool forceArrayView = false,
                                                bool useTransferQueue = false);
    VkSampler getTextureSampler(uint32_t mipLevels);

    // A background-loaded texture on a pure DMA queue only gets its pixel data copied (no blit
    // capability there); its mip chain / final layout transition is left pending here to be
    // finished on the main thread's graphics queue. Call once per frame.
    void finalizePendingMipmaps();
    // True while any texture is still waiting for finalizePendingMipmaps(); models loaded via the
    // transfer queue must not be marked ready until this is false again (image isn't in its final
    // shader-readable layout until then).
    bool hasPendingMipFinalize() const {
        return m_pendingFinalizeCount.load(std::memory_order_relaxed) > 0;
    }

private:
    /// <param name="is_flippingVertically"> keep it in 'true' by default since texture applies from top to bottom in
    /// Vulkan</param>
    VkResult loadImages(TextureFactory::Texture& outTexture, const std::vector<std::string>& textureFileNames, VkDevice device,
                        VkPhysicalDevice physicalDevice, VkQueue queue, VkCommandPool cmdBufPool,
                        bool is_miplevelsEnabling = true, bool is_flippingVertically = true, bool useTransferQueue = false);

private:
    // Image ready for its mip chain / final layout transition, deferred from a background transfer-queue upload.
    struct PendingMipFinalize {
        VkImage image{nullptr};
        uint32_t width{0u};
        uint32_t height{0u};
        uint32_t mipLevels{1u};
        size_t layerCount{1u};
    };

    const VulkanState& m_vkState;
    // Guards m_textures/m_samplers: Terrain/Skybox load on the main thread while other models may
    // load concurrently on a background loader thread using the transfer queue.
    // Recursive: create*Texture() holds the lock while calling getTextureSampler(), which also locks.
    std::recursive_mutex m_mutex;
    std::unordered_map<std::string, std::shared_ptr<Texture>, StringHash, std::equal_to<>> m_textures{};
    std::unordered_map<uint32_t, VkSampler> m_samplers{};
    VkPhysicalDeviceProperties m_properties{};
    std::function<void(TextureFactory::Texture* p)> mTextureDeleter{nullptr};
    std::vector<PendingMipFinalize> m_pendingFinalize{};
    std::atomic<int> m_pendingFinalizeCount{0};
};