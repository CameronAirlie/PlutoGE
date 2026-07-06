#include "PlutoGE/audio/AudioSystem.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <limits>
#include <string_view>

#ifdef _WIN32
#include <Windows.h>
#include <mmreg.h>
#include <xaudio2.h>
#ifdef max
#undef max
#endif
#ifdef min
#undef min
#endif
#endif

namespace PlutoGE::audio
{
    namespace
    {
#ifdef _WIN32
        constexpr std::uint16_t kWaveFormatPcm = 0x0001;
        constexpr std::uint16_t kWaveFormatFloat = 0x0003;
        constexpr std::uint16_t kWaveFormatExtensible = 0xFFFE;

        template <typename T>
        bool ReadPod(const std::vector<std::uint8_t> &bytes, std::size_t offset, T &value)
        {
            if (offset + sizeof(T) > bytes.size())
            {
                return false;
            }

            std::memcpy(&value, bytes.data() + offset, sizeof(T));
            return true;
        }

        struct DecodedClip
        {
            int channels = 0;
            int sampleRate = 0;
            std::vector<float> samples;

            [[nodiscard]] bool IsValid() const
            {
                return channels > 0 && sampleRate > 0 && !samples.empty();
            }
        };

        bool LoadWaveFile(const std::string &filePath, DecodedClip &clip)
        {
            std::ifstream input(filePath, std::ios::binary);
            if (!input.is_open())
            {
                return false;
            }

            std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
            if (bytes.size() < 44)
            {
                return false;
            }

            if (std::memcmp(bytes.data(), "RIFF", 4) != 0 || std::memcmp(bytes.data() + 8, "WAVE", 4) != 0)
            {
                return false;
            }

            std::size_t fmtOffset = 0;
            std::size_t fmtSize = 0;
            std::size_t dataOffset = 0;
            std::size_t dataSize = 0;

            for (std::size_t offset = 12; offset + 8 <= bytes.size();)
            {
                std::uint32_t chunkSize = 0;
                if (!ReadPod(bytes, offset + 4, chunkSize))
                {
                    return false;
                }

                const std::size_t chunkDataOffset = offset + 8;
                if (chunkDataOffset + chunkSize > bytes.size())
                {
                    return false;
                }

                const std::string_view chunkId(reinterpret_cast<const char *>(bytes.data() + offset), 4);
                if (chunkId == "fmt ")
                {
                    fmtOffset = chunkDataOffset;
                    fmtSize = chunkSize;
                }
                else if (chunkId == "data")
                {
                    dataOffset = chunkDataOffset;
                    dataSize = chunkSize;
                }

                offset = chunkDataOffset + chunkSize + (chunkSize & 1u);
            }

            if (fmtOffset == 0 || dataOffset == 0 || fmtSize < 16)
            {
                return false;
            }

            std::uint16_t formatTag = 0;
            std::uint16_t channels = 0;
            std::uint32_t sampleRate = 0;
            std::uint16_t bitsPerSample = 0;
            if (!ReadPod(bytes, fmtOffset, formatTag) ||
                !ReadPod(bytes, fmtOffset + 2, channels) ||
                !ReadPod(bytes, fmtOffset + 4, sampleRate) ||
                !ReadPod(bytes, fmtOffset + 14, bitsPerSample))
            {
                return false;
            }

            if (formatTag == kWaveFormatExtensible && fmtSize >= 40)
            {
                std::uint16_t subFormatTag = 0;
                if (!ReadPod(bytes, fmtOffset + 24, subFormatTag))
                {
                    return false;
                }
                formatTag = subFormatTag;
            }

            const std::size_t bytesPerSample = bitsPerSample / 8;
            if (channels == 0 || sampleRate == 0 || bytesPerSample == 0 || dataSize % bytesPerSample != 0)
            {
                return false;
            }

            const std::size_t sampleCount = dataSize / bytesPerSample;
            clip.channels = static_cast<int>(channels);
            clip.sampleRate = static_cast<int>(sampleRate);
            clip.samples.resize(sampleCount);

            const auto *sampleBytes = bytes.data() + dataOffset;
            for (std::size_t sampleIndex = 0; sampleIndex < sampleCount; ++sampleIndex)
            {
                const auto *sample = sampleBytes + sampleIndex * bytesPerSample;
                float converted = 0.0f;

                if (formatTag == kWaveFormatPcm)
                {
                    switch (bitsPerSample)
                    {
                    case 8:
                        converted = (static_cast<float>(sample[0]) - 128.0f) / 128.0f;
                        break;
                    case 16:
                    {
                        std::int16_t value = 0;
                        std::memcpy(&value, sample, sizeof(value));
                        converted = static_cast<float>(value) / 32768.0f;
                        break;
                    }
                    case 24:
                    {
                        std::int32_t value = static_cast<std::int32_t>(sample[0]) |
                                             (static_cast<std::int32_t>(sample[1]) << 8) |
                                             (static_cast<std::int32_t>(sample[2]) << 16);
                        if ((value & 0x00800000) != 0)
                        {
                            value |= ~0x00FFFFFF;
                        }
                        converted = static_cast<float>(value) / 8388608.0f;
                        break;
                    }
                    case 32:
                    {
                        std::int32_t value = 0;
                        std::memcpy(&value, sample, sizeof(value));
                        converted = static_cast<float>(value) / 2147483648.0f;
                        break;
                    }
                    default:
                        return false;
                    }
                }
                else if (formatTag == kWaveFormatFloat)
                {
                    if (bitsPerSample == 32)
                    {
                        std::memcpy(&converted, sample, sizeof(converted));
                    }
                    else if (bitsPerSample == 64)
                    {
                        double value = 0.0;
                        std::memcpy(&value, sample, sizeof(value));
                        converted = static_cast<float>(value);
                    }
                    else
                    {
                        return false;
                    }
                }
                else
                {
                    return false;
                }

                clip.samples[sampleIndex] = std::clamp(converted, -1.0f, 1.0f);
            }

            return clip.IsValid();
        }
#endif
    }

    bool AudioSystem::Initialize()
    {
#ifdef _WIN32
        if (m_initialized)
        {
            return true;
        }

        IXAudio2 *xaudio = nullptr;
        if (FAILED(XAudio2Create(&xaudio, 0, XAUDIO2_DEFAULT_PROCESSOR)) || !xaudio)
        {
            return false;
        }

        IXAudio2MasteringVoice *masterVoice = nullptr;
        if (FAILED(xaudio->CreateMasteringVoice(&masterVoice)) || !masterVoice)
        {
            xaudio->Release();
            return false;
        }

        XAUDIO2_VOICE_DETAILS details{};
        masterVoice->GetVoiceDetails(&details);

        m_xaudio = xaudio;
        m_masterVoice = masterVoice;
        m_outputChannels = (std::max)(details.InputChannels, 2u);
        m_initialized = true;
        return true;
#else
        return false;
#endif
    }

    void AudioSystem::Shutdown()
    {
        ClearEmitters();

#ifdef _WIN32
        if (auto *masterVoice = static_cast<IXAudio2MasteringVoice *>(m_masterVoice))
        {
            masterVoice->DestroyVoice();
        }

        if (auto *xaudio = static_cast<IXAudio2 *>(m_xaudio))
        {
            xaudio->Release();
        }
#endif

        m_masterVoice = nullptr;
        m_xaudio = nullptr;
        m_clipCache.clear();
        m_initialized = false;
    }

    bool AudioSystem::EnsureClipLoaded(const std::string &clipPath, const AudioClip *&clip)
    {
        if (const auto existing = m_clipCache.find(clipPath); existing != m_clipCache.end())
        {
            clip = &existing->second;
            return true;
        }

#ifdef _WIN32
        DecodedClip decodedClip;
        if (!LoadWaveFile(clipPath, decodedClip))
        {
            return false;
        }

        const auto [it, _] = m_clipCache.emplace(clipPath, AudioClip{
                                                               .channels = decodedClip.channels,
                                                               .sampleRate = decodedClip.sampleRate,
                                                               .samples = std::move(decodedClip.samples),
                                                           });
        clip = &it->second;
        return true;
#else
        (void)clipPath;
        clip = nullptr;
        return false;
#endif
    }

    void AudioSystem::DestroyVoice(std::uint64_t key)
    {
        const auto it = m_activeVoices.find(key);
        if (it == m_activeVoices.end())
        {
            return;
        }

#ifdef _WIN32
        auto *sourceVoice = static_cast<IXAudio2SourceVoice *>(it->second.sourceVoice);
        if (sourceVoice)
        {
            sourceVoice->Stop(0);
            sourceVoice->FlushSourceBuffers();
            sourceVoice->DestroyVoice();
        }
#endif

        m_activeVoices.erase(it);
    }

    void AudioSystem::ClearEmitters()
    {
        std::vector<std::uint64_t> keys;
        keys.reserve(m_activeVoices.size());
        for (const auto &[key, _] : m_activeVoices)
        {
            keys.push_back(key);
        }

        for (const auto key : keys)
        {
            DestroyVoice(key);
        }
    }

    bool AudioSystem::IsEmitterActive(std::uint64_t key) const
    {
        return m_activeVoices.find(key) != m_activeVoices.end();
    }

    float AudioSystem::ComputeAttenuation(const EmitterState &emitter, float distance) const
    {
        const float minDistance = (std::max)(0.001f, emitter.minDistance);
        const float maxDistance = (std::max)(minDistance, emitter.maxDistance);
        if (distance <= minDistance)
        {
            return 1.0f;
        }

        if (distance >= maxDistance)
        {
            return 0.0f;
        }

        const float normalized = (distance - minDistance) / (maxDistance - minDistance);
        return std::pow(1.0f - normalized, (std::max)(emitter.rolloff, 0.01f));
    }

    float AudioSystem::ComputePan(const ListenerState &listener, const EmitterState &emitter) const
    {
        const glm::vec3 offset = emitter.position - listener.position;
        const float distanceSquared = glm::dot(offset, offset);
        if (distanceSquared <= 0.000001f)
        {
            return 0.0f;
        }

        glm::vec3 right = glm::cross(listener.forward, listener.up);
        const float rightLengthSquared = glm::dot(right, right);
        if (rightLengthSquared <= 0.000001f)
        {
            return 0.0f;
        }

        right /= std::sqrt(rightLengthSquared);
        return std::clamp(glm::dot(offset / std::sqrt(distanceSquared), right), -1.0f, 1.0f);
    }

    void AudioSystem::UpdateVoice(ActiveVoice &voice, const ListenerState &listener, const EmitterState &emitter)
    {
#ifdef _WIN32
        auto *sourceVoice = static_cast<IXAudio2SourceVoice *>(voice.sourceVoice);
        if (!sourceVoice)
        {
            return;
        }

        sourceVoice->SetFrequencyRatio(std::clamp(emitter.pitch, 0.25f, 4.0f));

        float gain = std::max(emitter.volume, 0.0f);
        if (emitter.spatialized && listener.active && voice.channels == 1)
        {
            gain *= ComputeAttenuation(emitter, glm::distance(listener.position, emitter.position));
            gain *= 1.0f - 0.65f * std::clamp(emitter.occlusion, 0.0f, 1.0f);
            gain *= std::max(listener.masterVolume, 0.0f);

            std::vector<float> matrix(static_cast<std::size_t>(m_outputChannels), gain);
            if (m_outputChannels >= 2)
            {
                const float pan = ComputePan(listener, emitter);
                matrix[0] = gain * (pan <= 0.0f ? 1.0f : 1.0f - pan);
                matrix[1] = gain * (pan >= 0.0f ? 1.0f : 1.0f + pan);
                for (unsigned int channelIndex = 2; channelIndex < m_outputChannels; ++channelIndex)
                {
                    matrix[channelIndex] = gain * 0.65f;
                }
            }

            sourceVoice->SetOutputMatrix(static_cast<IXAudio2Voice *>(m_masterVoice), 1, m_outputChannels, matrix.data());

            XAUDIO2_FILTER_PARAMETERS filterParameters{};
            filterParameters.Type = LowPassFilter;
            filterParameters.OneOverQ = 1.0f;
            filterParameters.Frequency = std::clamp(1.0f - 0.55f * emitter.occlusion, XAUDIO2_MIN_FREQ_RATIO, XAUDIO2_MAX_FREQ_RATIO);
            sourceVoice->SetFilterParameters(&filterParameters);
            return;
        }

        gain *= listener.active ? std::max(listener.masterVolume, 0.0f) : 1.0f;
        sourceVoice->SetVolume(gain);

        XAUDIO2_FILTER_PARAMETERS filterParameters{};
        filterParameters.Type = LowPassFilter;
        filterParameters.OneOverQ = 1.0f;
        filterParameters.Frequency = XAUDIO2_MAX_FREQ_RATIO;
        sourceVoice->SetFilterParameters(&filterParameters);
#else
        (void)voice;
        (void)listener;
        (void)emitter;
#endif
    }

    void AudioSystem::StopInactiveEmitters(const std::vector<std::uint64_t> &activeKeys)
    {
        std::vector<std::uint64_t> staleKeys;
        staleKeys.reserve(m_activeVoices.size());
        for (const auto &[key, _] : m_activeVoices)
        {
            if (std::find(activeKeys.begin(), activeKeys.end(), key) == activeKeys.end())
            {
                staleKeys.push_back(key);
            }
        }

        for (const auto key : staleKeys)
        {
            DestroyVoice(key);
        }
    }

    void AudioSystem::Update(const ListenerState &listener, const std::vector<EmitterState> &emitters)
    {
#ifndef _WIN32
        (void)listener;
        (void)emitters;
        return;
#else
        if (!m_initialized)
        {
            return;
        }

        auto *xaudio = static_cast<IXAudio2 *>(m_xaudio);
        if (!xaudio || !m_masterVoice)
        {
            return;
        }

        std::vector<std::uint64_t> activeKeys;
        activeKeys.reserve(emitters.size());

        for (const auto &emitter : emitters)
        {
            activeKeys.push_back(emitter.key);

            if (!emitter.playing || emitter.clipPath.empty())
            {
                DestroyVoice(emitter.key);
                continue;
            }

            const AudioClip *clip = nullptr;
            if (!EnsureClipLoaded(emitter.clipPath, clip) || !clip)
            {
                DestroyVoice(emitter.key);
                continue;
            }

            auto currentVoice = m_activeVoices.find(emitter.key);
            const bool needsNewVoice = currentVoice == m_activeVoices.end() ||
                                       currentVoice->second.clipPath != emitter.clipPath ||
                                       currentVoice->second.looping != emitter.looping ||
                                       emitter.restartRequested;

            if (needsNewVoice)
            {
                DestroyVoice(emitter.key);

                WAVEFORMATEX waveFormat{};
                waveFormat.wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
                waveFormat.nChannels = static_cast<WORD>(clip->channels);
                waveFormat.nSamplesPerSec = static_cast<DWORD>(clip->sampleRate);
                waveFormat.wBitsPerSample = 32;
                waveFormat.nBlockAlign = static_cast<WORD>(clip->channels * sizeof(float));
                waveFormat.nAvgBytesPerSec = waveFormat.nSamplesPerSec * waveFormat.nBlockAlign;

                IXAudio2SourceVoice *sourceVoice = nullptr;
                if (FAILED(xaudio->CreateSourceVoice(&sourceVoice, &waveFormat)) || !sourceVoice)
                {
                    continue;
                }

                XAUDIO2_BUFFER buffer{};
                buffer.AudioBytes = static_cast<UINT32>(clip->samples.size() * sizeof(float));
                buffer.pAudioData = reinterpret_cast<const BYTE *>(clip->samples.data());
                buffer.Flags = XAUDIO2_END_OF_STREAM;
                buffer.LoopCount = emitter.looping ? XAUDIO2_LOOP_INFINITE : 0;
                if (FAILED(sourceVoice->SubmitSourceBuffer(&buffer)))
                {
                    sourceVoice->DestroyVoice();
                    continue;
                }

                if (!emitter.paused)
                {
                    sourceVoice->Start(0);
                }

                currentVoice = m_activeVoices.emplace(emitter.key, ActiveVoice{
                                                                       .sourceVoice = sourceVoice,
                                                                       .clipPath = emitter.clipPath,
                                                                       .channels = clip->channels,
                                                                       .paused = emitter.paused,
                                                                       .looping = emitter.looping,
                                                                   })
                                   .first;
            }
            else if (auto *sourceVoice = static_cast<IXAudio2SourceVoice *>(currentVoice->second.sourceVoice))
            {
                if (emitter.paused && !currentVoice->second.paused)
                {
                    sourceVoice->Stop(0);
                    currentVoice->second.paused = true;
                }
                else if (!emitter.paused && currentVoice->second.paused)
                {
                    sourceVoice->Start(0);
                    currentVoice->second.paused = false;
                }

                if (!currentVoice->second.looping)
                {
                    XAUDIO2_VOICE_STATE state{};
                    sourceVoice->GetState(&state);
                    if (state.BuffersQueued == 0)
                    {
                        DestroyVoice(emitter.key);
                        continue;
                    }
                }
            }

            if (auto activeVoice = m_activeVoices.find(emitter.key); activeVoice != m_activeVoices.end())
            {
                UpdateVoice(activeVoice->second, listener, emitter);
            }
        }

        StopInactiveEmitters(activeKeys);
#endif
    }
}
