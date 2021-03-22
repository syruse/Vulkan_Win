#include <windows.h>
#include "Win32Control.h"
#include "VulkanRenderer.h"

#include <iostream>


static constexpr std::wstring_view _appName{ L"Vulkan" };

static constexpr int16_t WINDOW_WIDTH = 1024;
static constexpr int16_t WINDOW_HEIGHT = 1024; 

VulkanRenderer _vulkanRenderer(_appName, WINDOW_WIDTH, WINDOW_HEIGHT);

int main(int argc, char** argv)
{

    _vulkanRenderer.init();

    /* program main loop */
    bool bQuit = false;
    MSG msg;
    while (!bQuit)
    {
        /* check for messages */
        if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
        {
            /* handle or dispatch messages */
            if (msg.message == WM_QUIT)
            {
                bQuit = TRUE;
            }
            else
            {
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }
        }
        else
        {
            _vulkanRenderer.renderScene();

            Sleep(1);
        }
    }


    /* destroy the window explicitly */
    

    return 0;
}

