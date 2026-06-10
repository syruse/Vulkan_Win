#pragma once

#include <utility>
#include <array>
#include <cstdint>

struct ResolutionEntry {
    int16_t width;
    int16_t height;
    const char* label;
};

class UI {
public:
    struct States {
        std::pair<const char*, bool> gpuAnimationEnabled{"favor animation calculation on GPU", true};
        std::pair<const char*, bool> placeHolder1{"placeHolder1", true};
        std::pair<const char*, bool> placeHolder2{"placeHolder2", true};
        bool resolutionChanged = false;
        int16_t nextWidth = 0;
        int16_t nextHeight = 0;
    };

    constexpr UI() : m_resolutions{{
        { 1280, 720, "1280x720" },
        { 1920, 1080, "1920x1080" },
        { 2560, 1440, "2560x1440" },
        { 3840, 2160, "3840x2160" }
    }} {}

    const States& updateAndDraw();

private:
    States mStates;
    std::array<ResolutionEntry, 4> m_resolutions;
    int m_selectedIdx = 0;
};
