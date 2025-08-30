#include "Skybox.h"
#include "I3DModel.h"
#include "Utils.h"
#include "PipelineCreatorTextured.h"
#include <assert.h>

/// Note: designed for VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST
const std::vector<Skybox::Vertex> _vertices = {
	// front
    {{-1.0, -1.0, 1.0}},
    {{1.0, -1.0, 1.0}},
	{{1.0, 1.0, 1.0}},
	{{-1.0, 1.0, 1.0}},
	// back
    {{-1.0, -1.0, -1.0}},
    {{1.0, -1.0, -1.0}},
	{{1.0, 1.0, -1.0}},
	{{-1.0, 1.0, -1.0}}};

const std::vector<uint32_t> _indices = {
	// front
	0, 1, 2, 2, 3, 0,
	// right
	1, 5, 6, 6, 2, 1,
	// back
	6, 5, 4, 4, 7, 6,
	// left
	4, 0, 3, 3, 7, 4,
	// bottom
	5, 4, 0, 0, 1, 5, // rejected by front face mode if no need to be drawn
	//0, 4, 5, 5, 1, 0, // accepted by front face mode
	// top
	6, 3, 2, 6, 7, 3};

void Skybox::init() {
	auto p_devide = m_vkState._core.getDevice();
	assert(p_devide);
	assert(m_pipelineCreatorTextured);
	assert(!m_textureFileNames.empty());
    assert(!m_instances.empty());

	auto texture = m_textureFactory.createCubeTexture(m_textureFileNames);
	if (!texture.expired()) {
		m_realMaterialId =
            m_pipelineCreatorTextured->createDescriptor(texture, m_textureFactory.getTextureSampler(texture.lock()->mipLevels));
	}

	Utils::createGeneral3in1Buffer(p_devide, m_vkState._core.getPhysDevice(), m_vkState._cmdBufPool, m_vkState._queue, _indices,
                                   _vertices, m_instances, m_verticesBufferOffset, m_instancesBufferOffset, m_generalBuffer,
                                   m_generalBufferMemory);
}

void Skybox::draw(VkCommandBuffer cmdBuf, uint32_t descriptorSetIndex, uint32_t dynamicOffset) const {
	assert(m_generalBuffer);
	assert(m_pipelineCreatorTextured);
	assert(m_pipelineCreatorTextured->getPipeline().get());

	vkCmdPushConstants(cmdBuf, m_pipelineCreatorTextured->getPipeline()->pipelineLayout, VulkanState::PUSH_CONSTANT_STAGE_FLAGS,
                       0, sizeof(VulkanState::PushConstant), &m_vkState._pushConstant);

	vkCmdBindPipeline(cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineCreatorTextured->getPipeline().get()->pipeline);

    VkBuffer vertexBuffers[] = {m_generalBuffer, m_generalBuffer};
    VkDeviceSize offsets[] = {m_verticesBufferOffset, m_instancesBufferOffset};
    vkCmdBindVertexBuffers(cmdBuf, 0, 2, vertexBuffers, offsets);
	vkCmdBindIndexBuffer(cmdBuf, m_generalBuffer, 0, VK_INDEX_TYPE_UINT32);

	vkCmdBindDescriptorSets(cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS,
							m_pipelineCreatorTextured->getPipeline().get()->pipelineLayout, 0, 1,
                            m_pipelineCreatorTextured->getDescriptorSet(descriptorSetIndex, m_realMaterialId), 1, &dynamicOffset);
	vkCmdDrawIndexed(cmdBuf, static_cast<uint32_t>(_indices.size()), 1U, 0U, 0, 0U);
}
