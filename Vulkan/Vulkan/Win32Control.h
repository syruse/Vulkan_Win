#pragma once

#include <windows.h>
#include "vulkan/vulkan.h"
#include "vulkan/vulkan_win32.h"
#include "vulkan/vk_sdk_platform.h"
#include "string"

class Win32Control
{
public:

    Win32Control(const std::wstring& pAppName);

    ~Win32Control();

    void Init(size_t Width, size_t Height);

    VkSurfaceKHR CreateSurface(VkInstance& inst);

private:

    HINSTANCE    m_hinstance;
    HWND         m_hwnd;
    std::wstring m_appName;
};

