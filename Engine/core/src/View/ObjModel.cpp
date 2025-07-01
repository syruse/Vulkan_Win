
#include "ObjModel.h"
#include "Constants.h"
#include "PipelineCreatorFootprint.h"
#include "PipelineCreatorTextured.h"
#include "Utils.h"

#define TINYOBJLOADER_IMPLEMENTATION
#include <tiny_obj_loader.h>
#include <algorithm>
#include <unordered_map>

void ObjModel::init() {
    auto p_device = m_vkState._core.getDevice();
    assert(p_device);

    std::vector<Vertex> vertices{};
    std::vector<uint32_t> indices{};
    // Note: we must have at least one instance to draw
    if (m_instances.empty()) {
        m_instances.push_back({glm::vec3(0.0f), 1.0f});
    }

    m_activeInstances.reserve(m_instances.size());
    m_activeInstances.assign(m_instances.begin(), m_instances.end());

    load(vertices, indices);
    Utils::createGeneralBuffer(p_device, m_vkState._core.getPhysDevice(), m_vkState._cmdBufPool, m_vkState._queue, indices,
                               vertices, m_verticesBufferOffset, m_generalBuffer, m_generalBufferMemory);
    {
        m_instancesBufferOffset = 0u;  // separete buffer for instances instead common buffer
        const VkDeviceSize instancesSize = sizeof(m_instances[0]) * m_instances.size();
        const VkDeviceSize bufferSize = m_instancesBufferOffset + instancesSize;
        for (size_t i = 0u; i < VulkanState::MAX_FRAMES_IN_FLIGHT; i++) {
            Utils::VulkanCreateBuffer(p_device, m_vkState._core.getPhysDevice(), bufferSize,
                                      VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                      m_instancesBuffer[i], m_instancesBufferMemory[i]);
            void* data;
            vkMapMemory(p_device, m_instancesBufferMemory[i], 0, bufferSize, 0, &data);
            memcpy((char*)data + m_instancesBufferOffset, m_instances.data(), instancesSize);
            vkUnmapMemory(p_device, m_instancesBufferMemory[i]);
        }
    }
}

void ObjModel::update(float deltaTimeMS, int animationID, bool onGPU, uint32_t currentImage, const glm::mat4& viewProj,
                      float z_far) {
    sortInstances(currentImage, viewProj, z_far);

    auto p_device = m_vkState._core.getDevice();
    assert(p_device);
    const VkDeviceSize instancesSize = sizeof(m_activeInstances[0]) * m_activeInstances.size();
    const VkDeviceSize bufferSize = m_instancesBufferOffset + instancesSize;

    void* data;
    vkMapMemory(p_device, m_instancesBufferMemory[currentImage], 0, bufferSize, 0, &data);
    memcpy((char*)data + m_instancesBufferOffset, m_activeInstances.data(), instancesSize);
    vkUnmapMemory(p_device, m_instancesBufferMemory[currentImage]);
}

void ObjModel::load(std::vector<Vertex>& vertices, std::vector<uint32_t>& indices) {
    assert(m_pipelineCreatorTextured);
    assert(!m_path.empty());
    vertices.clear();
    indices.clear();

    std::unordered_map<uint32_t, uint32_t> materialsMap{};  /// pair: local materialID: real materialID
    std::multimap<uint32_t, SubObject> subOjectsMap{};      /// For Sorting by material, keeps pair: local materialID: SubObject

    tinyobj::attrib_t attrib;
    std::vector<tinyobj::shape_t> shapes;
    std::vector<tinyobj::material_t> materials;
    std::string warn, err;

    std::string absPath = Utils::formPath(Constants::MODEL_DIR, m_path);

    if (!tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, absPath.c_str(), Constants::MODEL_DIR.data(), false,
                          false)) {
        Utils::printLog(ERROR_PARAM, (warn + err));
    }

    std::unordered_map<Vertex, uint32_t> uniqueVertices{};
    Vertex vertex{};

    glm::vec3 edge1{0.0f}, edge2{0.0f}, tangent{0.0f}, bitangent{0.0f};
    glm::vec2 deltaUV1{0.0f}, deltaUV2{0.0f};
    bool isBumpMappingValid{false};

    std::size_t indexAmount = 0u;
    for (const auto& shape : shapes) {
        indexAmount += shape.mesh.indices.size();
    }

    materialsMap.reserve(shapes.size());
    uniqueVertices.reserve(indexAmount);
    vertices.reserve(indexAmount);
    indices.reserve(indexAmount);

    std::size_t indecesOffset = 0u;

    for (const auto& shape : shapes) {
        assert(shape.mesh.material_ids.size() && materials.size());
        uint32_t materialId = shape.mesh.material_ids[0];
        uint32_t realMaterialId = 0u;

        isBumpMappingValid = materials[materialId].bump_texname.empty() ? false : true;

        if (materialsMap.find(materialId) == materialsMap.end()) {
            auto texture = m_textureFactory.create2DArrayTexture(
                isBumpMappingValid
                    ? std::vector<std::string>{materials[materialId].diffuse_texname, materials[materialId].bump_texname}
                    : std::vector<std::string>{materials[materialId].diffuse_texname});
            if (!texture.expired()) {
                realMaterialId = m_pipelineCreatorTextured->createDescriptor(
                    texture, m_textureFactory.getTextureSampler(texture.lock()->mipLevels));
                materialsMap.try_emplace(materialId, realMaterialId);
            } else {
                Utils::printLog(ERROR_PARAM, "couldn't create texture");
            }
        } else {
            realMaterialId = materialsMap[materialId];
        }

        for (std::size_t i = 0u; i < shape.mesh.indices.size(); ++i) {
            const auto& index = shape.mesh.indices[i];
            vertex.pos = {attrib.vertices[3 * index.vertex_index + 0] * m_vertexMagnitudeMultiplier,
                          attrib.vertices[3 * index.vertex_index + 1] * m_vertexMagnitudeMultiplier,
                          attrib.vertices[3 * index.vertex_index + 2] * m_vertexMagnitudeMultiplier};

            vertex.texCoord = {attrib.texcoords[2 * index.texcoord_index + 0], attrib.texcoords[2 * index.texcoord_index + 1]};

            vertex.normal = {attrib.normals[3 * index.normal_index + 0], attrib.normals[3 * index.normal_index + 1],
                             attrib.normals[3 * index.normal_index + 2]};

            /// Note: Obj format doesn't care about vertices reusing, let's take it on ourself
            if (uniqueVertices.find(vertex) == uniqueVertices.end()) {
                uniqueVertices[vertex] = static_cast<uint32_t>(vertices.size());
                float radius = glm::length(vertex.pos);
                if (radius > m_radius) {
                    m_radius = radius;
                }
                vertices.push_back(vertex);
            }

            indices.push_back(uniqueVertices[vertex]);

            if ((isBumpMappingValid) && ((i + 1u) % 3u == 0u)) {
                assert((static_cast<int>(indices.size()) - 3) >= 0);
                vertex.tangent.w = 1.0f;  // enabling bump-mapping
                auto& vert3 = vertices[indices[indices.size() - 1u]];
                auto& vert2 = vertices[indices[indices.size() - 2u]];
                auto& vert1 = vertices[indices[indices.size() - 3u]];
                edge1 = vert2.pos - vert1.pos;
                edge2 = vert3.pos - vert1.pos;
                deltaUV1 = vert2.texCoord - vert1.texCoord;
                deltaUV2 = vert3.texCoord - vert1.texCoord;

                float f = 1.0f / (deltaUV1.x * deltaUV2.y - deltaUV2.x * deltaUV1.y);

                tangent.x = f * (deltaUV2.y * edge1.x - deltaUV1.y * edge2.x);
                tangent.y = f * (deltaUV2.y * edge1.y - deltaUV1.y * edge2.y);
                tangent.z = f * (deltaUV2.y * edge1.z - deltaUV1.y * edge2.z);
                tangent = glm::normalize(tangent);
                vert1.tangent = glm::vec4(glm::normalize(glm::vec3(vert1.tangent) + tangent), 1.0f);
                vert2.tangent = glm::vec4(glm::normalize(glm::vec3(vert2.tangent) + tangent), 1.0f);
                vert3.tangent = glm::vec4(glm::normalize(glm::vec3(vert3.tangent) + tangent), 1.0f);

                bitangent.x = f * (-deltaUV2.x * edge1.x + deltaUV1.x * edge2.x);
                bitangent.y = f * (-deltaUV2.x * edge1.y + deltaUV1.x * edge2.y);
                bitangent.z = f * (-deltaUV2.x * edge1.z + deltaUV1.x * edge2.z);
                bitangent = glm::normalize(bitangent);
                vert1.bitangent = glm::normalize(vert1.bitangent + bitangent);
                vert2.bitangent = glm::normalize(vert2.bitangent + bitangent);
                vert3.bitangent = glm::normalize(vert3.bitangent + bitangent);
            }
        }

        /// Note: each subobject keeps index offset
        SubObject subObject{realMaterialId, indecesOffset, (indices.size() - indecesOffset), -1};
        subOjectsMap.emplace(materialId, subObject);
        indecesOffset = indices.size();

        /// Note: separation of tracks
        std::string shapeName = shape.name;
        m_Tracks.reserve(10);  // more than enough
        std::transform(shapeName.begin(), shapeName.end(), shapeName.begin(), ::tolower);
        if (m_pipelineCreatorFootprint && shapeName.find("track") != std::string::npos) {
            if (m_Tracks.empty()) {
                auto texture = m_textureFactory.create2DTexture(!materials[materialId].alpha_texname.empty()
                                                                    ? materials[materialId].alpha_texname
                                                                    : materials[materialId].diffuse_texname);
                if (!texture.expired()) {
                    subObject.realMaterialFootprintId = m_pipelineCreatorFootprint->createDescriptor(
                        texture, m_textureFactory.getTextureSampler(texture.lock()->mipLevels));
                }
            } else {
                // we must have common footprint texture for entire 3d model
                subObject.realMaterialFootprintId = m_Tracks[0].realMaterialFootprintId;
            }

            m_Tracks.push_back(subObject);
        }
    }

    /// Note: sorting by material executed
    m_SubObjects.clear();
    m_SubObjects.reserve(materialsMap.size());
    for (auto& materialID : materialsMap) {
        auto ret = subOjectsMap.equal_range(materialID.first);
        std::vector<SubObject> vec;
        vec.reserve(std::distance(ret.first, ret.second));
        for (auto it = ret.first; it != ret.second; ++it) {
            vec.emplace_back(it->second);
        }
        m_SubObjects.emplace_back(vec);
    }
}

void ObjModel::draw(VkCommandBuffer cmdBuf, uint32_t descriptorSetIndex, uint32_t dynamicOffset) const {
    assert(m_generalBuffer);
    assert(m_pipelineCreatorTextured);
    assert(m_pipelineCreatorTextured->getPipeline().get());

    if (m_pipelineCreatorTextured->isPushContantActive()) {
        vkCmdPushConstants(cmdBuf, m_pipelineCreatorTextured->getPipeline()->pipelineLayout,
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(VulkanState::PushConstant),
                           &m_vkState._pushConstant);
    }

    vkCmdBindPipeline(cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineCreatorTextured->getPipeline().get()->pipeline);

    VkBuffer vertexBuffers[] = {m_generalBuffer, m_instancesBuffer[descriptorSetIndex]};
    VkDeviceSize offsets[] = {m_verticesBufferOffset, m_instancesBufferOffset};
    vkCmdBindVertexBuffers(cmdBuf, 0, 2, vertexBuffers, offsets);
    vkCmdBindIndexBuffer(cmdBuf, m_generalBuffer, 0, VK_INDEX_TYPE_UINT32);

    for (const auto& subObjects : m_SubObjects) {
        if (subObjects.size()) {
            vkCmdBindDescriptorSets(
                cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineCreatorTextured->getPipeline().get()->pipelineLayout, 0, 1,
                m_pipelineCreatorTextured->getDescriptorSet(descriptorSetIndex, subObjects[0].realMaterialId), 1, &dynamicOffset);
            for (const auto& subObject : subObjects) {
                vkCmdDrawIndexed(cmdBuf, static_cast<uint32_t>(subObject.indexAmount), m_activeInstances.size(),
                                 static_cast<uint32_t>(subObject.indexOffset), 0, 0);
            }
        }
    }
}

void ObjModel::drawWithCustomPipeline(PipelineCreatorBase* pipelineCreator, VkCommandBuffer cmdBuf, uint32_t descriptorSetIndex,
                                      uint32_t dynamicOffset) const {
    assert(m_generalBuffer);
    assert(pipelineCreator);
    assert(pipelineCreator->getPipeline().get());

    if (pipelineCreator->isPushContantActive()) {
        vkCmdPushConstants(cmdBuf, pipelineCreator->getPipeline()->pipelineLayout,
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(VulkanState::PushConstant),
                           &m_vkState._pushConstant);
    }

    vkCmdBindPipeline(cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineCreator->getPipeline().get()->pipeline);

    VkBuffer vertexBuffers[] = {m_generalBuffer, m_instancesBuffer[descriptorSetIndex]};
    VkDeviceSize offsets[] = {m_verticesBufferOffset, m_instancesBufferOffset};
    vkCmdBindVertexBuffers(cmdBuf, 0, 2, vertexBuffers, offsets);
    vkCmdBindIndexBuffer(cmdBuf, m_generalBuffer, 0, VK_INDEX_TYPE_UINT32);

    for (const auto& subObjects : m_SubObjects) {
        if (subObjects.size()) {
            vkCmdBindDescriptorSets(cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineCreator->getPipeline().get()->pipelineLayout,
                                    0, 1, pipelineCreator->getDescriptorSet(descriptorSetIndex), 1, &dynamicOffset);
            for (const auto& subObject : subObjects) {
                vkCmdDrawIndexed(cmdBuf, static_cast<uint32_t>(subObject.indexAmount), m_activeInstances.size(),
                                 static_cast<uint32_t>(subObject.indexOffset), 0, 0);
            }
        }
    }
}

void ObjModel::drawFootprints(VkCommandBuffer cmdBuf, uint32_t descriptorSetIndex, uint32_t dynamicOffset) const {
    if (!m_pipelineCreatorFootprint || m_Tracks.empty()) {
        return;
    }
    assert(m_generalBuffer);
    assert(m_pipelineCreatorFootprint->getPipeline().get());

    if (m_pipelineCreatorFootprint->isPushContantActive()) {
        vkCmdPushConstants(cmdBuf, m_pipelineCreatorFootprint->getPipeline()->pipelineLayout,
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(VulkanState::PushConstant),
                           &m_vkState._pushConstant);
    }

    vkCmdBindPipeline(cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineCreatorFootprint->getPipeline().get()->pipeline);

    VkBuffer vertexBuffers[] = {m_generalBuffer, m_instancesBuffer[descriptorSetIndex]};
    VkDeviceSize offsets[] = {m_verticesBufferOffset, m_instancesBufferOffset};
    vkCmdBindVertexBuffers(cmdBuf, 0, 2, vertexBuffers, offsets);
    vkCmdBindIndexBuffer(cmdBuf, m_generalBuffer, 0, VK_INDEX_TYPE_UINT32);

    vkCmdBindDescriptorSets(
        cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineCreatorFootprint->getPipeline().get()->pipelineLayout, 0, 1,
        m_pipelineCreatorFootprint->getDescriptorSet(descriptorSetIndex, m_Tracks[0].realMaterialFootprintId), 1, &dynamicOffset);

    for (const auto& subObject : m_Tracks) {
        vkCmdDrawIndexed(cmdBuf, static_cast<uint32_t>(subObject.indexAmount), m_activeInstances.size(),
                         static_cast<uint32_t>(subObject.indexOffset), 0, 0);
    }
}
