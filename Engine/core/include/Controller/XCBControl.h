#pragma once

#ifdef __linux__

#include <SDL2/SDL.h>
#include "IControl.h"

class XCBControl : public IControl {
public:
    constexpr XCBControl(std::string_view appName, size_t width, size_t height)
        : IControl(appName, width, height) {
    }

    virtual ~XCBControl();

    virtual void init() override;

    virtual VkSurfaceKHR createSurface(VkInstance& inst) const override;

    virtual std::string_view getVulkanWindowSurfaceExtension() const override;

    virtual WindowQueueMSG processWindowQueueMSGs() override;

    virtual void imGuiNewFrame(VkCommandBuffer command_buffer) override;

private:
    SDL_Window* m_window{nullptr};
    bool m_isUiVisible{false};
    IControl::WindowQueueMSG m_windowQueueMsg{};
};

#endif
