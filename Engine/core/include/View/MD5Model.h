#pragma once

#include "I3DModel.h"

class MD5CudaAnimation;

class MD5Model : public I3DModel {
public:
    enum AnimationType : uint32_t {
        ANIMATION_TYPE_CUDA = 0U,
        ANIMATION_TYPE_CPU, 
        ANIMATION_TYPE_SIZE
    };
    MD5Model(std::string_view md5ModelFileName, std::string_view md5AnimFileName, const VulkanState& vulkanState,
             TextureFactory& textureFactory, PipelineCreatorTextured* pipelineCreatorTextured,
             PipelineCreatorFootprint* pipelineCreatorFootprint, float vertexMagnitudeMultiplier = 1.0f,
             float animationSpeedMultiplier = 1.0f, bool isSwapYZNeeded = true,
             const std::vector<Instance>& instances = {}) noexcept(true)
        : I3DModel(vulkanState, textureFactory, pipelineCreatorTextured, pipelineCreatorFootprint, vertexMagnitudeMultiplier,
                   instances),
          m_md5ModelFileName(md5ModelFileName),
          m_md5AnimFileName(md5AnimFileName),
          m_animationSpeedMultiplier(animationSpeedMultiplier),
          m_isSwapYZNeeded(isSwapYZNeeded) {
    }
    virtual ~MD5Model() {
        std::ignore = vkDeviceWaitIdle(m_vkState._core.getDevice());
        for (auto& buf : m_CUDAandCPUaccessibleBufs) {
            if (buf != nullptr) {
                vkDestroyBuffer(m_vkState._core.getDevice(), buf, nullptr);
            }
        }
        for (auto& mem : m_CUDAandCPUaccessibleMems) {
            if (mem != nullptr) {
                vkFreeMemory(m_vkState._core.getDevice(), mem, nullptr);
            }
        }
        vkDestroySemaphore(m_vkState._core.getDevice(), mVkCudaSyncObject, nullptr);
    }
    void init() override;
    void draw(VkCommandBuffer cmdBuf, uint32_t descriptorSetIndex, uint32_t dynamicOffset) const override;
    void drawWithCustomPipeline(PipelineCreatorBase* pipelineCreator, VkCommandBuffer cmdBuf, uint32_t descriptorSetIndex,
                                uint32_t dynamicOffset) const override;
    void drawFootprints(VkCommandBuffer cmdBuf, uint32_t descriptorSetIndex = 0U, uint32_t dynamicOffset = 0U) const override;
    void update(float deltaTimeMS, int animationID = 0u, bool onGPU = true, const glm::mat4& viewProj = glm::mat4(1.0f)) override;
    float radius() const override {
        return m_radius;
    }

private:
    bool loadMD5Anim();
    bool loadMD5Model(std::vector<VertexData>& vertices, std::vector<uint32_t>& indices);
    inline void swapYandZ(glm::vec3& vertexData);
    void updateAnimationChunk(std::size_t subsetId, std::size_t indexFrom, std::size_t indexTo);
    void calculateInterpolatedSkeleton(std::size_t animationID, std::size_t frame0, std::size_t frame1, float interpolation,
                                       std::size_t indexFrom, std::size_t indexTo);
    inline void updateAnimationOnGPU(float deltaTimeMS, std::size_t animationID);
    inline void updateAnimationOnCPU(float deltaTimeMS, std::size_t animationID);
    inline void waitForCudaSignal(uint32_t descriptorSetIndex) const;

private:
    std::string_view m_md5ModelFileName{};
    std::string_view m_md5AnimFileName{};
    float m_radius{0.0f};
    float m_animationSpeedMultiplier{1.0f};
    bool m_isSwapYZNeeded{true};  // due to different coordinate systems
    VkDeviceSize m_bufferSize{0u};
    md5_animation::Model3D m_MD5Model;
    // base intermediate animation as interpolation between neighbor frames animations
    // we keep it in memory to avoid allocations for each frame update
    std::vector<md5_animation::Joint> mInterpolatedSkeleton;
    // CUDA animation
    MD5CudaAnimation* mCudaAnimator{nullptr};
    VkSemaphore mVkCudaSyncObject{nullptr};  // for synchronization with CUDA
    bool mIsCudaCalculationRequested{false};
    mutable uint64_t mWaitCudaSignalValue{1};  // wait for CUDA signal value
    // CPU accessible buffer and CUDA\GPU accessible buffer
    VkBuffer m_CUDAandCPUaccessibleBufs[AnimationType::ANIMATION_TYPE_SIZE]{nullptr};
    VkDeviceMemory m_CUDAandCPUaccessibleMems[AnimationType::ANIMATION_TYPE_SIZE]{nullptr};
};
