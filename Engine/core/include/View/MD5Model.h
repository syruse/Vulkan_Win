#pragma once

#include "I3DModel.h"

class MD5Model : public I3DModel {
    struct MD5Vertex {
        // gpu data
        I3DModel::Vertex& gpuVertex;
        // for internal using
        int startWeight;
        int weightCount;
    };

    struct Joint {
        std::string name;
        int parentID;

        glm::vec3 pos;
        glm::quat orientation;
    };

    struct BoundingBox {
        glm::vec3 min;
        glm::vec3 max;
    };

    struct FrameData {
        int frameID;
        std::vector<float> frameData;
    };

    struct AnimJointInfo {
        std::string name;
        int parentID;

        int flags;
        int startIndex;
    };

    struct ModelAnimation {
        int numFrames;
        int numJoints;
        int frameRate;
        int numAnimatedComponents;

        float frameTime;
        float totalAnimTime;
        float currAnimTime;

        std::vector<AnimJointInfo> jointInfo;
        std::vector<BoundingBox> frameBounds;
        std::vector<Joint> baseFrameJoints;
        std::vector<FrameData> frameData;
        std::vector<std::vector<Joint>> frameSkeleton;
    };

    struct Weight {
        int jointID;
        float bias;
        glm::vec3 pos;
        glm::vec3 normal;
    };

    struct ModelSubset {
        int numTriangles;
        uint32_t realMaterialId{0u};
        uint32_t indexOffset{0u};
        uint32_t vertOffset{0u};

        std::vector<I3DModel::Vertex> gpuVertices;
        std::vector<MD5Vertex> vertices;
        std::vector<uint32_t> indices;
        std::vector<Weight> weights;
    };

    struct Model3D {
        int numSubsets;
        int numJoints;

        std::vector<Joint> joints;
        std::vector<ModelSubset> subsets;
        std::vector<ModelAnimation> animations;
    };

public:
    MD5Model(std::string_view md5ModelFileName, std::string_view md5AnimFileName, const VulkanState& vulkanState,
             TextureFactory& textureFactory, PipelineCreatorTextured* pipelineCreatorTextured,
             PipelineCreatorFootprint* pipelineCreatorFootprint, float vertexMagnitudeMultiplier = 1.0f,
             float animationSpeedMultiplier = 1.0f, bool isSwapYZNeeded = true) noexcept(true)
        : I3DModel(vulkanState, textureFactory, pipelineCreatorTextured, pipelineCreatorFootprint, vertexMagnitudeMultiplier),
          m_md5ModelFileName(md5ModelFileName),
          m_md5AnimFileName(md5AnimFileName),
          m_animationSpeedMultiplier(animationSpeedMultiplier),
          m_isSwapYZNeeded(isSwapYZNeeded) {
    }
    void init() override;
    void draw(VkCommandBuffer cmdBuf, uint32_t descriptorSetIndex, uint32_t dynamicOffset) const override;
    void drawWithCustomPipeline(PipelineCreatorBase* pipelineCreator, VkCommandBuffer cmdBuf, uint32_t descriptorSetIndex,
                                uint32_t dynamicOffset) const override;
    void drawFootprints(VkCommandBuffer cmdBuf, uint32_t descriptorSetIndex = 0U, uint32_t dynamicOffset = 0U) const override;
    void update(float deltaTimeMS, int animationID = 0u) override;
    float radius() const override {
        return m_radius;
    }

private:
    bool loadMD5Anim();
    bool loadMD5Model(std::vector<Vertex>& vertices, std::vector<uint32_t>& indices);
    void swapYandZ(glm::vec3& vertexData);

private:
    std::string_view m_md5ModelFileName{};
    std::string_view m_md5AnimFileName{};
    float m_radius{0.0f};
    float m_animationSpeedMultiplier{1.0f};
    bool m_isSwapYZNeeded{true};  // due to different coordinate systems
    VkDeviceSize m_bufferSize{0u};
    Model3D m_MD5Model;
};
