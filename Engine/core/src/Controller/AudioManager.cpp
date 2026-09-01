#include "AudioManager.h"
#include "Utils.h"

#include <algorithm>

namespace {
// Time to fully fade a looping sound in/out. Ramping volume instead of an immediate Stop()
// avoids an audible hard cut when a key (move/turret) is tapped and released quickly.
constexpr float kLoopFadeDurationMs = 700.0f;
}

AudioManager::AudioManager() = default;

AudioManager::~AudioManager() {
    if (!m_engine) {
        return;
    }
    for (auto& instance : m_loopInstances) {
        if (instance) {
            instance->Stop();
        }
    }
    m_engine->Suspend();
}

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

void AudioManager::init() {
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
}

void AudioManager::update(float deltaMs) {
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
}

void AudioManager::playLoop(Sound sound, float volume) {
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
}

void AudioManager::stopLoop(Sound sound) {
    const size_t idx = static_cast<size_t>(sound);
    if (idx >= kSoundCount) {
        return;
    }
    // Just request fade-out; update() ramps the volume down and stops the voice once inaudible.
    m_targetVolume[idx] = 0.0f;
}

void AudioManager::playOneShot(Sound sound, float volume) {
    const size_t idx = static_cast<size_t>(sound);
    if (idx >= kSoundCount || !m_effects[idx]) {
        return;
    }
    m_effects[idx]->Play(volume, 0.0f, 0.0f);
}
