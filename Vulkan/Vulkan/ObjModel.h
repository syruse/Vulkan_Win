#pragma once

#include "I3DModel.h"

class ObjModel : public I3DModel
{
public:
    ObjModel() = default;

    virtual void draw(VkCommandBuffer cmdBuf, std::function<void(uint16_t materialId)> descriptorBinding) override;

private:
    virtual void load(std::string_view path, TextureFactory* pTextureFactory, 
                      std::function<uint16_t(std::weak_ptr<TextureFactory::Texture>, VkSampler)> descriptorCreator, 
                      std::vector<Vertex> &vertices, std::vector<uint32_t> &indices) override;
};