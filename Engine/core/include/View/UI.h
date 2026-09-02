#pragma once

#include <utility>
#include <array>
#include <cstdint>

enum class UpscalerType   : uint8_t { None = 0, DLSS, XESS };
enum class UpscalerPreset : uint8_t { NativeAA = 0, UltraQuality, Quality, Balanced, Performance, UltraPerformance };

struct ResolutionEntry {
    int16_t width;
    int16_t height;
    const char* label;
};

class UI {
public:
    static constexpr int kDefaultResolutionIdx = 1;
    static constexpr std::array<ResolutionEntry, 4> kResolutions{{
        { 1280,  720,  "1280x720"  },
        { 1920,  1080, "1920x1080" },
        { 2560,  1440, "2560x1440" },
        { 3840,  2160, "3840x2160" }
    }};

    static constexpr ResolutionEntry defaultResolution() {
        return kResolutions[kDefaultResolutionIdx];
    }

    struct States {
        std::pair<const char*, bool> gpuAnimationEnabled{"favor animation calculation on GPU", true};
        std::pair<const char*, bool> placeHolder1{"placeHolder1", true};
        std::pair<const char*, bool> placeHolder2{"placeHolder2", true};
        bool resolutionChanged = false;
        int16_t nextWidth = kResolutions[kDefaultResolutionIdx].width;
        int16_t nextHeight = kResolutions[kDefaultResolutionIdx].height;

        UpscalerType   upscalerType    = UpscalerType::None;
        UpscalerPreset upscalerPreset = UpscalerPreset::UltraQuality;
        bool           upscalerChanged = false;
        bool           exitRequested = false;
    };

    constexpr UI() : m_resolutions(kResolutions) {}

    const States& updateAndDraw();

    // Toggles the centered "Loading..." overlay drawn on top of the menu while models stream in.
    void setLoading(bool isLoading) {
        mIsLoading = isLoading;
    }

    // Toggles the settings Menu window; independent of the Loading overlay so loading can be shown
    // even while the pause menu is closed.
    void setShowMenu(bool showMenu) {
        mShowMenu = showMenu;
    }

    void setUpscalerSupport(bool dlssSupported, bool xessSupported) {
        mDlssSupported = dlssSupported;
        mXessSupported = xessSupported;
    }

private:
    States mStates;
    std::array<ResolutionEntry, 4> m_resolutions;
    int m_selectedIdx = kDefaultResolutionIdx;
    bool mIsLoading = false;
    bool mShowMenu = true;
    bool mDlssSupported = false;
    bool mXessSupported = false;
};
