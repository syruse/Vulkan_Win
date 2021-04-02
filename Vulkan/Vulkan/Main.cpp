#include "VulkanRenderer.h"

#include <iostream>


static constexpr std::wstring_view _appName{ L"Vulkan" };

static constexpr int16_t WINDOW_WIDTH = 512;
static constexpr int16_t WINDOW_HEIGHT = 512; 

VulkanRenderer _vulkanRenderer(_appName, WINDOW_WIDTH, WINDOW_HEIGHT);

int main(int argc, char** argv)
{

    _vulkanRenderer.init();

    /* program main loop */

    bool bQuit = false;
    while (!bQuit)
    {
        bQuit = _vulkanRenderer.renderScene();
    }

    return 0;
}

