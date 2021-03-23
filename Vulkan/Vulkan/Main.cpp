#include <windows.h>
#include "Win32Control.h"
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
    MSG msg;
    while (!bQuit)
    {
        /**
        GetMessage does not return until a message matching the filter criteria is placed in the queue, whereas 
        PeekMessage returns immediately regardless of whether a message is in the queue.
        Remove any messages that may be in the queue of cpecified type like WM_QUIT
        */
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

