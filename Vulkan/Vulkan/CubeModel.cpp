
#include "CubeModel.h"
#include "Utils.h"
#include <unordered_map>

///Note: designed for VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST
const std::vector<I3DModel::Vertex> _vertices = 
{
    // front
    {{-1.0, -1.0, 1.0}},
    {{1.0, -1.0, 1.0}},
    {{1.0, 1.0, 1.0}},
    {{-1.0, 1.0, 1.0}},
    // back
    {{-1.0, -1.0, -1.0}},
    {{1.0, -1.0, -1.0}},
    {{1.0, 1.0, -1.0}},
    {{-1.0, 1.0, -1.0}}
};

const std::vector<uint16_t> _indices = 
{
    // front
    0, 1, 2,
    2, 3, 0,
    // right
    1, 5, 6,
    6, 2, 1,
    // back
    7, 6, 5,
    5, 4, 7,
    // left
    4, 0, 3,
    3, 7, 4,
    // bottom
    4, 5, 1,
    1, 0, 4,
    // top
    3, 2, 6,
    6, 7, 3
};

void CubeModel::load(std::string_view path, TextureFactory* pTextureFactory, 
                    std::function<uint16_t(std::weak_ptr<TextureFactory::Texture>, VkSampler)> descriptorCreator, 
                    std::vector<Vertex> &vertices, std::vector<uint32_t> &indices)
{
    assert(pTextureFactory);
    vertices.reserve(_vertices.size());
    indices.reserve(_indices.size());

    vertices.assign(_vertices.begin(), _vertices.end());
    indices.assign(_indices.begin(), _indices.end());

    uint16_t realMaterialId = 0u;

    auto texture = pTextureFactory->create2DTexture(path.data());
    if (!texture.expired())
    {
        realMaterialId = descriptorCreator(texture, pTextureFactory->getTextureSampler(texture.lock()->mipLevels));
    }
    else
    {
        Utils::printLog(ERROR_PARAM, "couldn't create texture");
    }

    m_SubObjects.clear();
    std::vector<SubObject> vec{{realMaterialId, 0u, indices.size()}};
    m_SubObjects.emplace_back(vec);
}

void CubeModel::draw(VkCommandBuffer cmdBuf, std::function<void(uint16_t materialId)> descriptorBinding)
{
    assert(descriptorBinding);
    assert(m_generalBuffer);

    VkBuffer vertexBuffers[] = { m_generalBuffer };
    VkDeviceSize offsets[] = { m_verticesBufferOffset };
    vkCmdBindVertexBuffers(cmdBuf, 0, 1, vertexBuffers, offsets);
    vkCmdBindIndexBuffer(cmdBuf, m_generalBuffer, 0, VK_INDEX_TYPE_UINT32);

    for (const auto& subObjects: m_SubObjects)
    {
        if (subObjects.size())
        {
            descriptorBinding(subObjects[0].realMaterialId);
            for (const auto &subObject : subObjects)
            {
                vkCmdDrawIndexed(cmdBuf, static_cast<uint32_t>(subObject.indexAmount), 1, static_cast<uint32_t>(subObject.indexOffset), 0, 0);
            }
        }
    }
}