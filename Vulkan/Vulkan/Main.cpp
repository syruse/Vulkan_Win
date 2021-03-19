#include <windows.h>
#include "Win32Control.h"
#include "VulkanCore.h"


std::wstring _appName = L"Vulkan";

static constexpr int16_t WINDOW_WIDTH = 1024;
static constexpr int16_t WINDOW_HEIGHT = 1024;
static constexpr int16_t MAX_FRAMES_IN_FLIGHT = 2;
static constexpr int16_t MAX_OBJECTS = 1;


Win32Control* _pWindowControl;
VulkanCore _core("Vulkan");

int main(int argc, char** argv)
{
    _pWindowControl = new Win32Control(_appName, WINDOW_WIDTH, WINDOW_HEIGHT);
    _pWindowControl->init();

    _core.Init(_pWindowControl);

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
            /* animation code goes here */

            Sleep(1);
        }
    }


    /* destroy the window explicitly */
    

    return 0;
}

