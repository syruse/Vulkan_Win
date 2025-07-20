#include "VulkanRenderer.h"

// you can activate debug mode by defining VK_DEBUG_ENABLED
// #define VK_DEBUG_ENABLED

static constexpr std::string_view _appName{"Vulkan"};
static constexpr int16_t WINDOW_WIDTH = 1920;
static constexpr int16_t WINDOW_HEIGHT = 1080;

int main(int argc, char** argv) {
    VulkanRenderer _vulkanRenderer(_appName, WINDOW_WIDTH, WINDOW_HEIGHT);
    _vulkanRenderer.init();

    /* program main loop */

    bool bQuit = false;
    while (!bQuit) {
        bQuit = !_vulkanRenderer.renderScene();
    }

    return 0;
}
