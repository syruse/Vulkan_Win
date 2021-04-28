#include "I3DModel.h"
#include "Utils.h"

#ifdef _WIN32
#include "windows.h"
///already included 'windows.h' with own implementations of aligned_alloc...
#elif __linux__
#include <cstring> // memcpy
#else            
///TO DO
#endif 

std::string I3DModel::MODEL_DIR = "models";

I3DModel::~I3DModel()
{
    vkDeviceWaitIdle(m_device);
    vkDestroyBuffer(m_device, m_generalBuffer, nullptr);
    vkFreeMemory(m_device, m_generalBufferMemory, nullptr);
}

void I3DModel::init(std::string_view path, VkDevice device, VkPhysicalDevice physicalDevice, VkCommandPool cmdBufPool, VkQueue queue,
                    std::function<uint16_t(std::weak_ptr<TextureFactory::Texture>, VkSampler)> descriptorCreator)
{
    assert(device);
    assert(physicalDevice);
    assert(cmdBufPool);
    assert(queue);
    assert(descriptorCreator);
    
    TextureFactory* pTextureFactory = &TextureFactory::init(device, physicalDevice, cmdBufPool, queue);
    std::vector<Vertex> vertices{};
    std::vector<uint32_t> indices{};

    m_device = device;
    m_physicalDevice = physicalDevice;
    load(path, pTextureFactory, descriptorCreator, vertices, indices);
    createGeneralBuffer(cmdBufPool, queue, indices, vertices);
}

void I3DModel::createGeneralBuffer(VkCommandPool cmdBufPool, VkQueue queue, std::vector<uint32_t>& indices, std::vector<Vertex>& vertices)
{
    const VkDeviceSize indicesSize = sizeof(indices[0]) * indices.size();
    m_verticesBufferOffset = indicesSize;
    const VkDeviceSize verticesSize = sizeof(vertices[0]) * vertices.size();
    const VkDeviceSize bufferSize = indicesSize + verticesSize;

    VkBuffer stagingBuffer;
    VkDeviceMemory stagingBufferMemory;
    Utils::VulkanCreateBuffer(m_device, m_physicalDevice, bufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, stagingBuffer, stagingBufferMemory);

    void* data;
    vkMapMemory(m_device, stagingBufferMemory, 0, bufferSize, 0, &data);
    memcpy(data, indices.data(), (size_t)indicesSize);
    memcpy((char*)data + m_verticesBufferOffset, vertices.data(), verticesSize);
    vkUnmapMemory(m_device, stagingBufferMemory);

    Utils::VulkanCreateBuffer(m_device, m_physicalDevice, bufferSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, m_generalBuffer, m_generalBufferMemory);

    Utils::VulkanCopyBuffer(m_device, queue, cmdBufPool, stagingBuffer, m_generalBuffer, bufferSize);

    vkDestroyBuffer(m_device, stagingBuffer, nullptr);
    vkFreeMemory(m_device, stagingBufferMemory, nullptr);
}
