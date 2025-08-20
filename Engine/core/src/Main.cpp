#include "VulkanRenderer.h"

// you can activate debug mode by defining VK_DEBUG_ENABLED
// #define VK_DEBUG_ENABLED

static constexpr std::string_view _appName{"Vulkan"};
static constexpr int16_t WINDOW_WIDTH = 1920;
static constexpr int16_t WINDOW_HEIGHT = 1080;

int main(int argc, char** argv) {
    int16_t width = WINDOW_WIDTH;
    int16_t height = WINDOW_HEIGHT;
#ifdef _WIN32
    MONITORINFO monitorInfo;
    monitorInfo.cbSize = sizeof(monitorInfo);
    GetMonitorInfo(MonitorFromWindow(nullptr, MONITOR_DEFAULTTOPRIMARY), &monitorInfo);

    width = monitorInfo.rcMonitor.right - monitorInfo.rcMonitor.left;
    height = monitorInfo.rcMonitor.bottom - monitorInfo.rcMonitor.top;
#endif

    VulkanRenderer _vulkanRenderer(_appName, width, height);
    _vulkanRenderer.init();

    /* program main loop */

    bool bQuit = false;
    while (!bQuit) {
        bQuit = !_vulkanRenderer.renderScene();
    }

    return 0;
}
