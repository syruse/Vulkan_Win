#pragma once

#include <utility>
#include <string>

class UI {
public:
    struct States {
        std::pair<std::string, bool> gpuAnimationEnabled{"favor animation calculation on GPU", false};
        std::pair<std::string, bool> placeHolder1{"placeHolder1", true};
        std::pair<std::string, bool> placeHolder2{"placeHolder2", true};
    };

    const States& updateAndDraw();

private:
    States mStates;
};
