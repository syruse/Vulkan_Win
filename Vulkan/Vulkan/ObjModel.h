#pragma once

#include "I3DModel.h"

class ObjModel: public I3DModel
{
public:

ObjModel() = default;

virtual void loadModel(std::string_view path) override;

private:

};