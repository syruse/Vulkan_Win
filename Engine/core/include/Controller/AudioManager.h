#pragma once

#if defined(_WIN32)
#include <Audio.h>
#else
#include <SDL.h>
#endif

#include <array>
#include <memory>
#include <string_view>

// Centralized DirectXTK (XAudio2) audio wrapper. Owns the AudioEngine and pre-loaded
// SoundEffect assets used by the game. Runs on the main/game thread — XAudio2 already
// mixes on its own internal worker thread, so update() only does lightweight bookkeeping.

// Uses DirectXTK on Windows and SDL audio on Unix platforms.
class AudioManager {
public:
    enum class Sound {
        ForestAmbient,  // background loop: forest
        TankEngine,     // background loop: tank engine idle
        TurretRotate,   // loops while the barrel is being rotated (Q/E)
        TankMove,       // loops while the tank is moving (tracks)
        TankFire,       // one-shot: cannon fire
        TreeBreak,      // one-shot: tree falling/breaking
        Count
    };

    AudioManager();
    ~AudioManager();

    AudioManager(const AudioManager&) = delete;
    AudioManager& operator=(const AudioManager&) = delete;

    // Loads all sound assets from the "sounds/" folder next to the executable.
    // Missing/placeholder assets are logged and skipped so the game keeps running silently.
    void init();

    // Must be called once per frame from the render/game loop; deltaMs drives loop fade speed.
    void update(float deltaMs);

    // Requests a looping instance to fade in to the given volume; safe to call every frame.
    void playLoop(Sound sound, float volume = 1.0f);
    // Requests a looping instance to fade out; safe to call every frame.
    // A short fade (instead of an immediate Stop) avoids cutting off audio mid-word on quick key taps.
    void stopLoop(Sound sound);
    // Fires a one-shot sound effect (fire-and-forget, managed internally by AudioEngine).
    void playOneShot(Sound sound, float volume = 1.0f);

private:
    static constexpr size_t kSoundCount = static_cast<size_t>(Sound::Count);
#if defined(_WIN32)
    static std::wstring_view fileNameFor(Sound sound);

    std::unique_ptr<DirectX::AudioEngine> m_engine;
    std::array<std::unique_ptr<DirectX::SoundEffect>, kSoundCount> m_effects;
    std::array<std::unique_ptr<DirectX::SoundEffectInstance>, kSoundCount> m_loopInstances;
#else
    static const char* fileNameFor(Sound sound);

    struct Clip {
        Uint8* data = nullptr;
        Uint32 length = 0;
    };

    struct Voice {
        size_t clip = 0;
        Uint32 position = 0;
        float volume = 0.0f;
        bool looping = false;
        bool active = false;
    };

    static void audioCallback(void* userData, Uint8* stream, int length);
    void mixAudio(Uint8* stream, int length);

    SDL_AudioDeviceID m_audioDevice = 0;
    SDL_AudioSpec m_audioSpec{};
    std::array<Clip, kSoundCount> m_clips;
    std::array<Voice, kSoundCount> m_loopVoices;
    std::array<Voice, kSoundCount> m_oneShotVoices;
#endif
    // Current/target volume per loop slot, ramped in update() to fade instead of hard-cutting.
    std::array<float, kSoundCount> m_currentVolume{};
    std::array<float, kSoundCount> m_targetVolume{};
};
