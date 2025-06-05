#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <vector>

struct VertexData {
    glm::vec3 pos{0.0f};
    glm::vec3 normal{0.0f};
    glm::vec2 texCoord{0.0f};
    // extra attributes for TBN matrix

    // last component indicates legality of bump-mapping applying
    // 3d model may contain no bump texture for some sub models that's why we need to enable\disable bump-mapping
    // there should be no perf drop for using additional 4th component since anyway atributes passed as vec4 per one location
    glm::vec4 tangent{0.0f};
    glm::vec3 bitangent{0.0f};
};

namespace md5_animation {
struct MD5Vertex {
    // gpu data
    uint32_t gpuVertexIndex;
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

    std::vector<VertexData> gpuVertices;
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
}  // namespace md5_animation