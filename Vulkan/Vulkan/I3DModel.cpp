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
    vkDestroyBuffer(m_device, m_vertexBuffer, nullptr);
    vkFreeMemory(m_device, m_vertexBufferMemory, nullptr);
    vkDestroyBuffer(m_device, m_indexBuffer, nullptr);
    vkFreeMemory(m_device, m_indexBufferMemory, nullptr);
}

void I3DModel::init(std::string_view path, VkDevice device, VkPhysicalDevice physicalDevice, VkCommandPool cmdBufPool, VkQueue queue)
{
    assert(device);
    assert(physicalDevice);
    assert(cmdBufPool);
    assert(queue);
    
    mp_textureFactory = &TextureFactory::init(device, physicalDevice, cmdBufPool, queue);

    m_device = device;
    m_physicalDevice = physicalDevice;
    load(path);
    createVertexBuffer(cmdBufPool, queue);
    createIndexBuffer(cmdBufPool, queue);
    m_vertices.clear();
    m_indices.clear();
}

void I3DModel::createVertexBuffer(VkCommandPool cmdBufPool, VkQueue queue)
{
    VkDeviceSize bufferSize = sizeof(m_vertices[0]) * m_vertices.size();

    VkBuffer stagingBuffer;
    VkDeviceMemory stagingBufferMemory;
    Utils::VulkanCreateBuffer(m_device, m_physicalDevice, bufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, stagingBuffer, stagingBufferMemory);

    void* data;
    vkMapMemory(m_device, stagingBufferMemory, 0, bufferSize, 0, &data);
    memcpy(data, m_vertices.data(), (size_t)bufferSize);
    vkUnmapMemory(m_device, stagingBufferMemory);

    Utils::VulkanCreateBuffer(m_device, m_physicalDevice, bufferSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, m_vertexBuffer, m_vertexBufferMemory);

    Utils::VulkanCopyBuffer(m_device, queue, cmdBufPool, stagingBuffer, m_vertexBuffer, bufferSize);

    vkDestroyBuffer(m_device, stagingBuffer, nullptr);
    vkFreeMemory(m_device, stagingBufferMemory, nullptr);
}

void I3DModel::createIndexBuffer(VkCommandPool cmdBufPool, VkQueue queue) 
{
    VkDeviceSize bufferSize = sizeof(m_indices[0]) * m_indices.size();

    VkBuffer stagingBuffer;
    VkDeviceMemory stagingBufferMemory;
    Utils::VulkanCreateBuffer(m_device, m_physicalDevice, bufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, stagingBuffer, stagingBufferMemory);

    void* data;
    vkMapMemory(m_device, stagingBufferMemory, 0, bufferSize, 0, &data);
    memcpy(data, m_indices.data(), (size_t)bufferSize);
    vkUnmapMemory(m_device, stagingBufferMemory);

    Utils::VulkanCreateBuffer(m_device, m_physicalDevice, bufferSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, m_indexBuffer, m_indexBufferMemory);

    Utils::VulkanCopyBuffer(m_device, queue, cmdBufPool, stagingBuffer, m_indexBuffer, bufferSize);

    vkDestroyBuffer(m_device, stagingBuffer, nullptr);
    vkFreeMemory(m_device, stagingBufferMemory, nullptr);
}