#include "AudioManager.h"
#include "Utils.h"

#include <algorithm>
#if !defined(_WIN32)
#include <cstring>
#endif

namespace {
// Time to fully fade a looping sound in/out. Ramping volume instead of an immediate Stop()
// avoids an audible hard cut when a key (move/turret) is tapped and released quickly.
constexpr float kLoopFadeDurationMs = 700.0f;
}

AudioManager::AudioManager() = default;

AudioManager::~AudioManager() {
#if defined(_WIN32)
    if (!m_engine) {
        return;
    }
    for (auto& instance : m_loopInstances) {
        if (instance) {
            instance->Stop();
        }
    }
    m_engine->Suspend();
#else
    if (m_audioDevice != 0) {
        SDL_LockAudioDevice(m_audioDevice);
        SDL_PauseAudioDevice(m_audioDevice, 1);
        SDL_UnlockAudioDevice(m_audioDevice);
        SDL_CloseAudioDevice(m_audioDevice);
    }
    for (auto& clip : m_clips) {
        SDL_free(clip.data);
    }
#endif
}

#if defined(_WIN32)
std::wstring_view AudioManager::fileNameFor(Sound sound) {
    switch (sound) {
        case Sound::ForestAmbient: return L"sounds/forest_ambient.wav";
        case Sound::TankEngine:    return L"sounds/tank_engine.wav";
        case Sound::TurretRotate:  return L"sounds/turret_rotate.wav";
        case Sound::TankMove:      return L"sounds/tank_move.wav";
        case Sound::TankFire:      return L"sounds/tank_fire.wav";
        case Sound::TreeBreak:     return L"sounds/tree_break.wav";
        default:                   return L"";
    }
}
#else
const char* AudioManager::fileNameFor(Sound sound) {
    switch (sound) {
        case Sound::ForestAmbient: return "sounds/forest_ambient.wav";
        case Sound::TankEngine:    return "sounds/tank_engine.wav";
        case Sound::TurretRotate:  return "sounds/turret_rotate.wav";
        case Sound::TankMove:      return "sounds/tank_move.wav";
        case Sound::TankFire:      return "sounds/tank_fire.wav";
        case Sound::TreeBreak:     return "sounds/tree_break.wav";
        default:                   return "";
    }
}
#endif

void AudioManager::init() {
#if defined(_WIN32)
    DirectX::AUDIO_ENGINE_FLAGS flags = DirectX::AudioEngine_Default;
#ifdef _DEBUG
    flags = static_cast<DirectX::AUDIO_ENGINE_FLAGS>(flags | DirectX::AudioEngine_Debug);
#endif

    try {
        m_engine = std::make_unique<DirectX::AudioEngine>(flags);
    } catch (const std::exception& e) {
        Utils::printLog(ERROR_PARAM, "Failed to create AudioEngine: ", e.what());
        return;
    }

    for (size_t i = 0; i < kSoundCount; ++i) {
        const std::wstring_view fileName = fileNameFor(static_cast<Sound>(i));
        if (fileName.empty()) {
            continue;
        }
        try {
            m_effects[i] = std::make_unique<DirectX::SoundEffect>(m_engine.get(), fileName.data());
        } catch (const std::exception& e) {
            // Placeholder/missing asset: log and continue so the game keeps running without this sound.
            Utils::printLog(ERROR_PARAM, "Failed to load sound asset, will stay silent: ", e.what());
        }
    }
#else
    SDL_AudioSpec desired{};
    desired.freq = 48000;
    desired.format = AUDIO_S16SYS;
    desired.channels = 2;
    desired.samples = 1024;
    desired.callback = &AudioManager::audioCallback;
    desired.userdata = this;

    m_audioDevice = SDL_OpenAudioDevice(nullptr, 0, &desired, &m_audioSpec, 0);
    if (m_audioDevice == 0) {
        Utils::printLog(ERROR_PARAM, "Failed to open SDL audio device: ", SDL_GetError());
        return;
    }

    for (size_t i = 0; i < kSoundCount; ++i) {
        SDL_AudioSpec sourceSpec{};
        Uint8* sourceData = nullptr;
        Uint32 sourceLength = 0;
        if (SDL_LoadWAV(fileNameFor(static_cast<Sound>(i)), &sourceSpec, &sourceData, &sourceLength) == nullptr) {
            Utils::printLog(ERROR_PARAM, "Failed to load sound asset: ", SDL_GetError());
            continue;
        }

        SDL_AudioCVT converter{};
        if (SDL_BuildAudioCVT(&converter, sourceSpec.format, sourceSpec.channels, sourceSpec.freq,
                              m_audioSpec.format, m_audioSpec.channels, m_audioSpec.freq) < 0) {
            SDL_FreeWAV(sourceData);
            continue;
        }

        if (converter.needed) {
            converter.len = static_cast<int>(sourceLength);
            converter.buf = static_cast<Uint8*>(SDL_malloc(converter.len * converter.len_mult));
            if (converter.buf == nullptr) {
                SDL_FreeWAV(sourceData);
                continue;
            }
            std::memcpy(converter.buf, sourceData, sourceLength);
            SDL_FreeWAV(sourceData);
            if (SDL_ConvertAudio(&converter) < 0) {
                SDL_free(converter.buf);
                continue;
            }
            m_clips[i] = {converter.buf, static_cast<Uint32>(converter.len_cvt)};
        } else {
            m_clips[i] = {sourceData, sourceLength};
        }
    }
    SDL_PauseAudioDevice(m_audioDevice, 0);
#endif
}

void AudioManager::update(float deltaMs) {
#if defined(_WIN32)
    if (!m_engine) {
        return;
    }

    const float maxStep = deltaMs / kLoopFadeDurationMs;
    for (size_t i = 0; i < kSoundCount; ++i) {
        auto& instance = m_loopInstances[i];
        if (!instance) {
            continue;
        }

        if (m_currentVolume[i] < m_targetVolume[i]) {
            m_currentVolume[i] = std::min(m_targetVolume[i], m_currentVolume[i] + maxStep);
        } else if (m_currentVolume[i] > m_targetVolume[i]) {
            m_currentVolume[i] = std::max(m_targetVolume[i], m_currentVolume[i] - maxStep);
        }

        if (instance->GetState() == DirectX::PLAYING) {
            instance->SetVolume(m_currentVolume[i]);
        }

        // Fully faded out: stop the (already inaudible) voice so it doesn't keep mixing, but keep
        // the instance around so playLoop() can restart it later without reloading the effect.
        if (m_targetVolume[i] <= 0.0f && m_currentVolume[i] <= 0.0f && instance->GetState() == DirectX::PLAYING) {
            instance->Stop(true);
        }
    }

    if (!m_engine->Update() && m_engine->IsCriticalError()) {
        // No audio device / device lost: keep the game running silently.
    }
#else
    if (m_audioDevice == 0) {
        return;
    }
    const float maxStep = deltaMs / kLoopFadeDurationMs;
    SDL_LockAudioDevice(m_audioDevice);
    for (size_t i = 0; i < kSoundCount; ++i) {
        if (m_currentVolume[i] < m_targetVolume[i]) {
            m_currentVolume[i] = std::min(m_targetVolume[i], m_currentVolume[i] + maxStep);
        } else if (m_currentVolume[i] > m_targetVolume[i]) {
            m_currentVolume[i] = std::max(m_targetVolume[i], m_currentVolume[i] - maxStep);
        }
        m_loopVoices[i].volume = m_currentVolume[i];
        if (m_targetVolume[i] <= 0.0f && m_currentVolume[i] <= 0.0f) {
            m_loopVoices[i].active = false;
        }
    }
    SDL_UnlockAudioDevice(m_audioDevice);
#endif
}

void AudioManager::playLoop(Sound sound, float volume) {
#if defined(_WIN32)
    const size_t idx = static_cast<size_t>(sound);
    if (idx >= kSoundCount || !m_effects[idx]) {
        return;
    }

    m_targetVolume[idx] = volume;

    auto& instance = m_loopInstances[idx];
    if (!instance) {
        instance = m_effects[idx]->CreateInstance();
    }
    if (instance->GetState() != DirectX::PLAYING) {
        // Start silent and let update() ramp it up, avoiding a click on (re)start too.
        m_currentVolume[idx] = 0.0f;
        instance->SetVolume(0.0f);
        instance->Play(true);
    }
#else
    const size_t idx = static_cast<size_t>(sound);
    if (idx >= kSoundCount || m_audioDevice == 0 || m_clips[idx].data == nullptr) {
        return;
    }
    SDL_LockAudioDevice(m_audioDevice);
    m_targetVolume[idx] = volume;
    auto& voice = m_loopVoices[idx];
    if (!voice.active) {
        voice = {idx, 0, 0.0f, true, true};
    }
    SDL_UnlockAudioDevice(m_audioDevice);
#endif
}

void AudioManager::stopLoop(Sound sound) {
#if defined(_WIN32)
    const size_t idx = static_cast<size_t>(sound);
    if (idx >= kSoundCount) {
        return;
    }
    // Just request fade-out; update() ramps the volume down and stops the voice once inaudible.
    m_targetVolume[idx] = 0.0f;
#else
    const size_t idx = static_cast<size_t>(sound);
    if (idx >= kSoundCount || m_audioDevice == 0) {
        return;
    }
    SDL_LockAudioDevice(m_audioDevice);
    m_targetVolume[idx] = 0.0f;
    SDL_UnlockAudioDevice(m_audioDevice);
#endif
}

void AudioManager::playOneShot(Sound sound, float volume) {
#if defined(_WIN32)
    const size_t idx = static_cast<size_t>(sound);
    if (idx >= kSoundCount || !m_effects[idx]) {
        return;
    }
    m_effects[idx]->Play(volume, 0.0f, 0.0f);
#else
    const size_t idx = static_cast<size_t>(sound);
    if (idx >= kSoundCount || m_audioDevice == 0 || m_clips[idx].data == nullptr) {
        return;
    }
    SDL_LockAudioDevice(m_audioDevice);
    auto& voice = m_oneShotVoices[idx];
    voice = {idx, 0, volume, false, true};
    SDL_UnlockAudioDevice(m_audioDevice);
#endif
}

#if !defined(_WIN32)
void AudioManager::audioCallback(void* userData, Uint8* stream, int length) {
    static_cast<AudioManager*>(userData)->mixAudio(stream, length);
}

void AudioManager::mixAudio(Uint8* stream, int length) {
    std::memset(stream, 0, static_cast<size_t>(length));
    auto* output = reinterpret_cast<Sint16*>(stream);
    const int outputSamples = length / static_cast<int>(sizeof(Sint16));

    auto mixVoice = [&](Voice& voice) {
        if (!voice.active || voice.volume <= 0.0f) {
            return;
        }
        const Clip& clip = m_clips[voice.clip];
        const auto* input = reinterpret_cast<const Sint16*>(clip.data);
        const int clipSamples = static_cast<int>(clip.length / sizeof(Sint16));
        for (int i = 0; i < outputSamples; ++i) {
            if (voice.position >= static_cast<Uint32>(clipSamples)) {
                if (!voice.looping) {
                    voice.active = false;
                    break;
                }
                voice.position = 0;
            }
            output[i] = static_cast<Sint16>(std::clamp(
                static_cast<int>(output[i]) + static_cast<int>(input[voice.position++] * voice.volume),
                -32768, 32767));
        }
    };

    for (auto& voice : m_loopVoices) {
        mixVoice(voice);
    }
    for (auto& voice : m_oneShotVoices) {
        mixVoice(voice);
    }
}
#endif
