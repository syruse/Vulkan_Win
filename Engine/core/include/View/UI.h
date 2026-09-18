#pragma once

#include <utility>
#include <array>
#include <cstdint>
#include <functional>

enum class UpscalerType   : uint8_t { None = 0, DLSS, XESS };
enum class UpscalerPreset : uint8_t { NativeAA = 0, UltraQuality, Quality, Balanced, Performance, UltraPerformance };
enum class FoliageQuality : uint8_t { Minimum = 0, Medium, Maximum };
enum class ShadowQuality : uint8_t { Minimum = 0, Medium, Maximum };

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
        std::pair<const char*, bool> tessellationEnabled{"Tessellation", true};
        FoliageQuality foliageQuality = FoliageQuality::Minimum;
        bool foliageQualityChanged = false;
        ShadowQuality shadowQuality = ShadowQuality::Minimum;
        bool shadowQualityChanged = false;
        std::pair<const char*, bool> rayTracedShadowsEnabled{"Ray traced shadows", false};
        bool rayTracedShadowsChanged = false;
        std::pair<const char*, bool> ssaoEnabled{"SSAO", false};
        bool resolutionChanged = false;
        int16_t nextWidth = kResolutions[kDefaultResolutionIdx].width;
        int16_t nextHeight = kResolutions[kDefaultResolutionIdx].height;

        UpscalerType   upscalerType    = UpscalerType::None;
        UpscalerPreset upscalerPreset = UpscalerPreset::UltraQuality;
        bool           upscalerChanged = false;
        bool           exitRequested = false;
    };

    constexpr UI() : m_resolutions(kResolutions) {}

    const States& updateAndDraw(const std::function<void()>& drawOverlay = {});

    // Toggles the centered "Loading..." overlay drawn on top of the menu while models stream in.
    void setLoading(bool isLoading) {
        mIsLoading = isLoading;
    }

    // Toggles the centered welcome/briefing popup shown once loading finishes, until Enter is pressed.
    void setWelcome(bool showWelcome) {
        mShowWelcome = showWelcome;
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

    void setRayTracingSupport(bool supported) {
        mRayTracingSupported = supported;
        if (!supported) {
            mStates.rayTracedShadowsEnabled.second = false;
        }
    }

    // Enable GPU animation by default when support becomes available, without overriding the user's toggle afterward.
    void setGpuAnimationSupport(bool supported) {
        const bool supportBecameAvailable = supported && !mGpuAnimationSupported;
        mGpuAnimationSupported = supported;
        if (!supported) {
            mStates.gpuAnimationEnabled.second = false;
        } else if (supportBecameAvailable) {
            mStates.gpuAnimationEnabled.second = true;
        }
    }

    void setCombatState(float health, float reloadProgress, uint32_t shellCount, float sprintProgress, uint32_t score,
                        bool isGameOver) {
        mHealth = health;
        mReloadProgress = reloadProgress;
        mShellCount = shellCount;
        mSprintProgress = sprintProgress;
        mScore = score;
        mIsGameOver = isGameOver;
    }

private:
    States mStates;
    std::array<ResolutionEntry, 4> m_resolutions;
    int m_selectedIdx = kDefaultResolutionIdx;
    bool mIsLoading = false;
    bool mShowWelcome = false;
    bool mShowMenu = true;
    bool mDlssSupported = false;
    bool mXessSupported = false;
    bool mRayTracingSupported = false;
    bool mGpuAnimationSupported = false;
    float mHealth = 1.0f;
    float mReloadProgress = 1.0f;
    uint32_t mShellCount = 10u;
    float mSprintProgress = 1.0f;
    uint32_t mScore = 0u;
    bool mIsGameOver = false;
};
