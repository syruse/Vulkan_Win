#pragma once

#include "CubeModel.h"

class Skybox: public CubeModel
{
public:
    Skybox() = default;

    void init();

    virtual void draw(VkCommandBuffer cmdBuf, std::function<void(uint16_t materialId)> descriptorBinding) override;

private:
    virtual void load(std::string_view path, TextureFactory* pTextureFactory, 
                      std::function<uint16_t(std::weak_ptr<TextureFactory::Texture>, VkSampler)> descriptorCreator, 
                      std::vector<Vertex> &vertices, std::vector<uint32_t> &indices) override;
};