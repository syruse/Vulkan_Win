#include "VulkanRenderer.h"

static constexpr std::wstring_view _appName{L"Vulkan"};
static constexpr int16_t WINDOW_WIDTH = 512;
static constexpr int16_t WINDOW_HEIGHT = 512;

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
