#pragma once

#include <windows.h>
#include "vulkan/vulkan.h"
#include "vulkan/vulkan_win32.h"
#include "vulkan/vk_sdk_platform.h"
#include "IControl.h"
#include "string"

class Win32Control : public IControl
{
public:

    Win32Control(const std::wstring& pAppName, size_t width, size_t height);

    ~Win32Control();

    virtual void init() override;

    virtual VkSurfaceKHR createSurface(VkInstance& inst) const override;

private:
    HINSTANCE    m_hinstance;
    HWND         m_hwnd;
};

