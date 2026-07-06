#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>

namespace PlutoGE::audio
{
    struct ListenerState
    {
        bool active = false;
        glm::vec3 position{0.0f};
        glm::vec3 forward{0.0f, 0.0f, -1.0f};
        glm::vec3 up{0.0f, 1.0f, 0.0f};
        float masterVolume = 1.0f;
    };

    struct EmitterState
    {
        std::uint64_t key = 0;
        std::string clipPath;
        glm::vec3 position{0.0f};
        bool playing = false;
        bool paused = false;
        bool looping = false;
        bool spatialized = true;
        bool restartRequested = false;
        float volume = 1.0f;
        float pitch = 1.0f;
        float minDistance = 1.0f;
        float maxDistance = 25.0f;
        float rolloff = 1.0f;
        float occlusion = 0.0f;
    };

    class AudioSystem
    {
    public:
        struct AudioClip
        {
            int channels = 0;
            int sampleRate = 0;
            std::vector<float> samples;
        };

        struct ActiveVoice
        {
            void *sourceVoice = nullptr;
            std::string clipPath;
            int channels = 0;
            bool paused = false;
            bool looping = false;
        };

        AudioSystem() = default;
        ~AudioSystem() = default;

        bool Initialize();
        void Shutdown();
        void Update(const ListenerState &listener, const std::vector<EmitterState> &emitters);
        void ClearEmitters();
        bool IsEmitterActive(std::uint64_t key) const;
        bool IsAvailable() const { return m_initialized; }

    private:
        bool EnsureClipLoaded(const std::string &clipPath, const AudioClip *&clip);
        void DestroyVoice(std::uint64_t key);
        void StopInactiveEmitters(const std::vector<std::uint64_t> &activeKeys);
        void UpdateVoice(ActiveVoice &voice, const ListenerState &listener, const EmitterState &emitter);
        float ComputeAttenuation(const EmitterState &emitter, float distance) const;
        float ComputePan(const ListenerState &listener, const EmitterState &emitter) const;

        bool m_initialized = false;
        void *m_xaudio = nullptr;
        void *m_masterVoice = nullptr;
        unsigned int m_outputChannels = 2;
        std::unordered_map<std::string, AudioClip> m_clipCache;
        std::unordered_map<std::uint64_t, ActiveVoice> m_activeVoices;
    };
}
