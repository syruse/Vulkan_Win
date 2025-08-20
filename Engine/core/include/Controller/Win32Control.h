#pragma once

#ifdef _WIN32

#include <windows.h>
#include "IControl.h"
#include "string"
#include "vulkan/vulkan.h"
#include <Keyboard.h>
#include <Mouse.h>

class Win32Control : public IControl {
public:
    constexpr Win32Control(std::string_view appName, size_t width, size_t height)
        : IControl(appName, width, height), m_hinstance(nullptr), m_hwnd(0) {
    }

    virtual ~Win32Control();

    virtual void init() override;

    virtual VkSurfaceKHR createSurface(VkInstance& inst) const override;

    virtual std::string_view getVulkanWindowSurfaceExtension() const override;

    virtual WindowQueueMSG processWindowQueueMSGs() override;

private:
    virtual void imGuiNewFrame(VkCommandBuffer command_buffer) override;

private:
    HINSTANCE m_hinstance;
    HWND m_hwnd;
    std::unique_ptr<DirectX::Keyboard> mKeyboard{nullptr};
    std::unique_ptr<DirectX::Mouse> mMouse{nullptr};
};

#endif
