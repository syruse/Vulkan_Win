
#include "ObjModel.h"
#include "Utils.h"
#define TINYOBJLOADER_IMPLEMENTATION
#include <tiny_obj_loader.h>
#include <unordered_map>

void ObjModel::load(std::string_view path)
{
    assert(mp_textureFactory);
    tinyobj::attrib_t attrib;
    std::vector<tinyobj::shape_t> shapes;
    std::vector<tinyobj::material_t> materials;
    std::string warn, err;

    std::string absPath;
    Utils::formPath(MODEL_DIR.c_str(), path, absPath);

    if (!tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, absPath.c_str(), MODEL_DIR.c_str(), false, false))
    {
        Utils::printLog(ERROR_PARAM, (warn + err));
    }

    std::unordered_map<Vertex, uint32_t> uniqueVertices{};
    Vertex vertex{};

    m_indecesAmount = 0u;
    for (const auto &shape : shapes)
    {
        m_indecesAmount += shape.mesh.indices.size();
    }

    uniqueVertices.reserve(m_indecesAmount);
    m_vertices.reserve(m_indecesAmount);
    m_indices.reserve(m_indecesAmount);

    for (const auto &shape : shapes)
    {
        assert(shape.mesh.material_ids.size() && materials.size());
        uint32_t materialId = shape.mesh.material_ids[0];

        if(m_materials.count(materialId) == 0)
        {
            auto texture = mp_textureFactory->create2DTexture(materials[materialId].diffuse_texname.c_str());
            uint32_t realMaterialId = m_descriptorCreator(texture, mp_textureFactory->getTextureSampler(texture->mipLevels));
            m_materials.try_emplace(materialId, realMaterialId);
        }

        for (const auto &index : shape.mesh.indices)
        {
            vertex.pos = {
                attrib.vertices[3 * index.vertex_index + 0],
                attrib.vertices[3 * index.vertex_index + 1],
                attrib.vertices[3 * index.vertex_index + 2]};

            vertex.texCoord = {
                attrib.texcoords[2 * index.texcoord_index + 0],
                attrib.texcoords[2 * index.texcoord_index + 1]};

            vertex.normal = {
                attrib.normals[3 * index.vertex_index + 0],
                attrib.normals[3 * index.vertex_index + 1],
                attrib.normals[3 * index.vertex_index + 2]};

            if (uniqueVertices.count(vertex) == 0)
            {
                uniqueVertices[vertex] = static_cast<uint32_t>(m_vertices.size());
                m_vertices.push_back(vertex);
            }

            m_indices.push_back(uniqueVertices[vertex]);
        }
    }
}

void ObjModel::draw(VkCommandBuffer cmdBuf, std::function<void(uint16_t materialId)> descriptorBinding)
{
    assert(descriptorBinding);
    assert(m_vertexBuffer);
    assert(m_indecesAmount);

    descriptorBinding(m_materials[0]);// to FIX

    VkBuffer vertexBuffers[] = { m_vertexBuffer };
    VkDeviceSize offsets[] = { 0 };
    vkCmdBindVertexBuffers(cmdBuf, 0, 1, vertexBuffers, offsets);
    vkCmdBindIndexBuffer(cmdBuf, m_indexBuffer, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(cmdBuf, static_cast<uint32_t>(m_indecesAmount), 1, 0, 0, 0);
}