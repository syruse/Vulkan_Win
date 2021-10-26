#pragma once
#include "PipelineCreatorBase.h"

class PipelineCreatorTextured: public PipelineCreatorBase
{
public:

    constexpr PipelineCreatorTextured(std::string_view vertShader, std::string_view fragShader, uint32_t subpass = 0u)
    :PipelineCreatorBase(vertShader, fragShader, subpass)
    {}


private:

    virtual void createPipeline() override;


};