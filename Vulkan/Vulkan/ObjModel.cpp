
#include "ObjModel.h"
#include "Constants.h"
#include "Utils.h"

#define TINYOBJLOADER_IMPLEMENTATION
#include <tiny_obj_loader.h>
#include <unordered_map>


void ObjModel::init(const std::function<uint16_t(std::weak_ptr<TextureFactory::Texture> texture, VkSampler sampler,
        VkDescriptorSetLayout descriptorSetLayout)>& descriptorCreator)
{
    auto p_devide = m_vkState._core.getDevice();
    assert(p_devide);
    assert(descriptorCreator);

    std::vector<Vertex> vertices{};
    std::vector<uint32_t> indices{};

    load(descriptorCreator, vertices, indices);
    Utils::createGeneralBuffer(p_devide, m_vkState._core.getPhysDevice(), m_vkState._cmdBufPool, m_vkState._queue, indices, vertices,
        m_verticesBufferOffset, m_generalBuffer, m_generalBufferMemory);
}

void ObjModel::load(std::function<uint16_t(std::weak_ptr<TextureFactory::Texture> texture, VkSampler sampler,
                        VkDescriptorSetLayout descriptorSetLayout)> descriptorCreator,
                    std::vector<Vertex> &vertices, std::vector<uint32_t> &indices)
{
    assert(descriptorCreator);
    assert(m_pipelineCreatorBase);
    assert(!m_path.empty());
    vertices.clear();
    indices.clear();

    std::unordered_map<uint16_t, uint16_t> materialsMap{}; /// pair: local materialID: real materialID
    std::multimap<uint16_t, SubObject> subOjectsMap{}; /// For Sorting by material, keeps pair: local materialID: SubObject

    tinyobj::attrib_t attrib;
    std::vector<tinyobj::shape_t> shapes;
    std::vector<tinyobj::material_t> materials;
    std::string warn, err;

    std::string absPath = Utils::formPath(Constants::MODEL_DIR, m_path);

    if (!tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, absPath.c_str(), Constants::MODEL_DIR.data(), false, false))
    {
        Utils::printLog(ERROR_PARAM, (warn + err));
    }

    std::unordered_map<Vertex, uint32_t> uniqueVertices{};
    Vertex vertex{};

    std::size_t indexAmount = 0u;
    for (const auto &shape : shapes)
    {
        indexAmount += shape.mesh.indices.size();
    }

    materialsMap.reserve(shapes.size());
    uniqueVertices.reserve(indexAmount);
    vertices.reserve(indexAmount);
    indices.reserve(indexAmount);

    std::size_t indecesOffset = 0u;

    for (const auto &shape : shapes)
    {
        assert(shape.mesh.material_ids.size() && materials.size());
        uint16_t materialId = shape.mesh.material_ids[0];
        uint16_t realMaterialId = 0u;

        if(materialsMap.count(materialId) == 0)
        {
            auto texture = m_textureFactory.create2DTexture(materials[materialId].diffuse_texname.c_str());
            if (!texture.expired())
            {
                realMaterialId = descriptorCreator(texture, m_textureFactory.getTextureSampler(texture.lock()->mipLevels),
                    *m_pipelineCreatorBase->getDescriptorSetLayout().get());
                materialsMap.try_emplace(materialId, realMaterialId);
            }
            else
            {
                Utils::printLog(ERROR_PARAM, "couldn't create texture");
            }
        }
        else
        {
            realMaterialId = materialsMap[materialId];
        }

        for (const auto &index : shape.mesh.indices)
        {
            vertex.pos = {
                attrib.vertices[3 * index.vertex_index + 0] * m_vertexMagnitudeMultiplier,
                attrib.vertices[3 * index.vertex_index + 1] * m_vertexMagnitudeMultiplier,
                attrib.vertices[3 * index.vertex_index + 2] * m_vertexMagnitudeMultiplier };

            vertex.texCoord = {
                attrib.texcoords[2 * index.texcoord_index + 0],
                attrib.texcoords[2 * index.texcoord_index + 1]};

            vertex.normal = {
                attrib.normals[3 * index.normal_index + 0],
                attrib.normals[3 * index.normal_index + 1],
                attrib.normals[3 * index.normal_index + 2]};

            /// Note: Obj format doesn't care about vertices reusing, let's take it on ourself
            if (uniqueVertices.count(vertex) == 0)
            {
                uniqueVertices[vertex] = static_cast<uint32_t>(vertices.size());
                vertices.push_back(vertex);
            }

            indices.push_back(uniqueVertices[vertex]);
        }
        /// Note: each subobject keeps index offset
        subOjectsMap.emplace(materialId, SubObject{realMaterialId, indecesOffset, (indices.size() - indecesOffset)});
        indecesOffset = indices.size();
    }

    /// Note: sorting by material executed
    m_SubObjects.clear();
    m_SubObjects.reserve(materialsMap.size());
    for (auto& materialID: materialsMap)
    { 
        auto ret = subOjectsMap.equal_range(materialID.first);
        std::vector<SubObject> vec;
        vec.reserve(std::distance(ret.first, ret.second));
        for (auto it=ret.first; it != ret.second; ++it)
        {
            vec.emplace_back(it->second);
        }
        m_SubObjects.emplace_back(vec);
    }
}

void ObjModel::draw(VkCommandBuffer cmdBuf, std::function<void(uint16_t materialId, VkPipelineLayout pipelineLayout)> descriptorBinding)
{
    assert(descriptorBinding);
    assert(m_generalBuffer);
    assert(m_pipelineCreatorBase);
    assert(m_pipelineCreatorBase->getPipeline().get());

    vkCmdBindPipeline(cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineCreatorBase->getPipeline().get()->pipeline);

    VkBuffer vertexBuffers[] = { m_generalBuffer };
    VkDeviceSize offsets[] = { m_verticesBufferOffset };
    vkCmdBindVertexBuffers(cmdBuf, 0, 1, vertexBuffers, offsets);
    vkCmdBindIndexBuffer(cmdBuf, m_generalBuffer, 0, VK_INDEX_TYPE_UINT32);

    for (const auto& subObjects: m_SubObjects)
    {
        if (subObjects.size())
        {
            descriptorBinding(subObjects[0].realMaterialId, m_pipelineCreatorBase->getPipeline().get()->pipelineLayout);
            for (const auto &subObject : subObjects)
            {
                vkCmdDrawIndexed(cmdBuf, static_cast<uint32_t>(subObject.indexAmount), 1, static_cast<uint32_t>(subObject.indexOffset), 0, 0);
            }
        }
    }
}