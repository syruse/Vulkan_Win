#pragma once

#include <vulkan/vulkan.h>
#include <string>
#include "UI.h"

class ImDrawData;
class IControl {
public:
    struct WindowQueueMSG {
        enum Button { 
            LEFT = 1, RIGHT = 2, UP = 4, DONW = 8
        };
        bool isQuited = false;
        bool isResized = false;
        uint32_t buttonFlag = 0;
        uint32_t width = 0;
        uint32_t height = 0;
        uint32_t mouseX = 0;
        uint32_t mouseY = 0;
        bool hmiRenderData = false;
        const UI::States* hmiStates = nullptr;

        void reset() {
            isQuited = false;
            isResized = false;
            width = 0u;
            height = 0u;
        }
    };

    constexpr IControl(std::string_view appName, uint32_t width, uint32_t height)
        : m_appName(appName), m_width(width), m_height(height) {
    }

    virtual ~IControl() = default;

    virtual void init() = 0;

    virtual VkSurfaceKHR createSurface(VkInstance& inst) const = 0;

    inline std::string_view getAppName() const {
        return m_appName;
    }

    virtual std::string_view getVulkanWindowSurfaceExtension() const {
        return "";
    }

    inline uint32_t getWidth() const {
        return m_width;
    }

    inline uint32_t getHeight() const {
        return m_height;
    }

    virtual WindowQueueMSG processWindowQueueMSGs() = 0;

    virtual void imGuiNewFrame(VkCommandBuffer command_buffer) = 0;

protected:
    std::string_view m_appName;
    uint32_t m_width;
    uint32_t m_height;
    UI mUi;
};