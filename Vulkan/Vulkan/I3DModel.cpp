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
    Utils::createGeneralBuffer(device, physicalDevice, cmdBufPool, queue, indices, vertices,
        m_verticesBufferOffset, m_generalBuffer, m_generalBufferMemory);
}
