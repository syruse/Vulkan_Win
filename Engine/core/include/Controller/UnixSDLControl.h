#pragma once

#if defined(__linux__) || defined(__APPLE__)

#include <SDL2/SDL.h>
#include "IControl.h"

class UnixSDLControl : public IControl {
public:
    constexpr UnixSDLControl(std::string_view appName, size_t width, size_t height)
        : IControl(appName, width, height) {
    }

    virtual ~UnixSDLControl();

    virtual void init() override;

    virtual VkSurfaceKHR createSurface(VkInstance& inst) const override;

    virtual std::string_view getVulkanWindowSurfaceExtension() const override;

    virtual WindowQueueMSG processWindowQueueMSGs() override;

    virtual void imGuiNewFrame(VkCommandBuffer command_buffer, const std::function<void()>& drawOverlay = {}) override;

private:
    SDL_Window* m_window{nullptr};
    bool m_isUiVisible{false};
    bool m_enterPressedEdge{false};
    bool m_firePressedEdge{false};
    IControl::WindowQueueMSG m_windowQueueMsg{};
};

#endif
