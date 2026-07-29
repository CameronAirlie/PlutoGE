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
#include <unordered_set>

#if defined(PLUTOGE_USE_OPENAL_SOFT)
#define AL_ALEXT_PROTOTYPES
#include <AL/al.h>
#include <AL/alc.h>
#include <AL/alext.h>
#include <AL/efx.h>
#endif

#ifdef _WIN32
#include <Windows.h>
#include <mmreg.h>
#include <xaudio2.h>
#include <x3daudio.h>
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
#if defined(PLUTOGE_USE_OPENAL_SOFT) || defined(_WIN32)
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

#if defined(PLUTOGE_USE_OPENAL_SOFT)
        std::int16_t FloatToInt16(float value)
        {
            return static_cast<std::int16_t>(std::clamp(value, -1.0f, 1.0f) * 32767.0f);
        }

        std::vector<std::int16_t> ConvertMonoToInt16(const std::vector<float> &samples)
        {
            std::vector<std::int16_t> converted;
            converted.reserve(samples.size());
            for (const float sample : samples)
            {
                converted.push_back(FloatToInt16(sample));
            }

            return converted;
        }

        std::vector<std::int16_t> ConvertToStereoInt16(const std::vector<float> &samples, int channels)
        {
            if (channels <= 1)
            {
                std::vector<std::int16_t> converted;
                converted.reserve(samples.size() * 2u);
                for (const float sample : samples)
                {
                    const std::int16_t value = FloatToInt16(sample);
                    converted.push_back(value);
                    converted.push_back(value);
                }
                return converted;
            }

            const std::size_t frameCount = samples.size() / static_cast<std::size_t>(channels);
            std::vector<std::int16_t> converted;
            converted.reserve(frameCount * 2u);
            for (std::size_t frameIndex = 0; frameIndex < frameCount; ++frameIndex)
            {
                const auto baseIndex = frameIndex * static_cast<std::size_t>(channels);
                converted.push_back(FloatToInt16(samples[baseIndex]));
                converted.push_back(FloatToInt16(samples[baseIndex + 1u]));
            }

            return converted;
        }

        bool HasOpenALExtension(const char *name)
        {
            return alIsExtensionPresent(name) == AL_TRUE;
        }

        bool HasOpenALDeviceExtension(ALCdevice *device, const char *name)
        {
            return device != nullptr && alcIsExtensionPresent(device, name) == ALC_TRUE;
        }
#endif

#ifdef _WIN32
        float ToXAudioFilterFrequency(float cutoffHz, int sampleRate)
        {
            const float safeSampleRate = static_cast<float>((std::max)(sampleRate, 1));
            const float nyquist = safeSampleRate * 0.5f;
            const float clampedCutoff = std::clamp(cutoffHz, 10.0f, nyquist * 0.95f);
            const float radians = 2.0f * std::sin(3.14159265358979323846f * clampedCutoff / safeSampleRate);
            return std::clamp(radians, XAUDIO2_MIN_FREQ_RATIO, XAUDIO2_MAX_FREQ_RATIO);
        }

        float InterpolateLogFrequency(float highHz, float lowHz, float amount)
        {
            const float clampedAmount = std::clamp(amount, 0.0f, 1.0f);
            return std::exp(std::log(highHz) + (std::log(lowHz) - std::log(highHz)) * clampedAmount);
        }

        X3DAUDIO_VECTOR ToX3DAudioVector(const glm::vec3 &value)
        {
            return X3DAUDIO_VECTOR{value.x, value.y, value.z};
        }

        glm::vec3 SafeNormalize(const glm::vec3 &value, const glm::vec3 &fallback)
        {
            const float lengthSquared = glm::dot(value, value);
            if (!std::isfinite(lengthSquared) || lengthSquared <= 0.000001f)
            {
                return fallback;
            }

            return value / std::sqrt(lengthSquared);
        }

        float OnePoleCoefficient(float cutoffHz, int sampleRate)
        {
            const float dt = 1.0f / static_cast<float>((std::max)(sampleRate, 1));
            const float rc = 1.0f / (2.0f * 3.14159265358979323846f * (std::max)(cutoffHz, 10.0f));
            return dt / (rc + dt);
        }
#endif
    }

    bool AudioSystem::Initialize()
    {
#if defined(PLUTOGE_USE_OPENAL_SOFT)
        if (m_initialized)
        {
            return true;
        }

        ALCdevice *device = alcOpenDevice(nullptr);
        if (!device)
        {
            return false;
        }

        const bool hrtfAvailable = alcIsExtensionPresent(device, "ALC_SOFT_HRTF") == ALC_TRUE;
        ALCint hrtfAttributes[] = {
#ifdef ALC_HRTF_SOFT
            ALC_HRTF_SOFT,
            ALC_TRUE,
#endif
            0,
        };
        ALCcontext *context = alcCreateContext(device, hrtfAvailable ? hrtfAttributes : nullptr);
        if (!context)
        {
            alcCloseDevice(device);
            return false;
        }

        if (alcMakeContextCurrent(context) != ALC_TRUE)
        {
            alcDestroyContext(context);
            alcCloseDevice(device);
            return false;
        }

        alDistanceModel(AL_INVERSE_DISTANCE_CLAMPED);
        alDopplerFactor(0.15f);
        alSpeedOfSound(343.3f);

        m_openAlDevice = device;
        m_openAlContext = context;
        m_usingOpenAl = true;
        m_openAlEfxAvailable = HasOpenALDeviceExtension(device, "ALC_EXT_EFX");
        m_initialized = true;
        return true;
#else
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
        if (FAILED(xaudio->CreateMasteringVoice(&masterVoice, 2, XAUDIO2_DEFAULT_SAMPLERATE)) || !masterVoice)
        {
            xaudio->Release();
            return false;
        }

        XAUDIO2_VOICE_DETAILS details{};
        masterVoice->GetVoiceDetails(&details);

        m_xaudio = xaudio;
        m_masterVoice = masterVoice;
        m_outputChannels = 2;
        DWORD outputChannelMask = 0;
        if (SUCCEEDED(masterVoice->GetChannelMask(&outputChannelMask)) && outputChannelMask != 0)
        {
            X3DAudioInitialize(outputChannelMask, X3DAUDIO_SPEED_OF_SOUND, reinterpret_cast<BYTE *>(m_spatialAudioHandle.data()));
            m_outputChannelMask = outputChannelMask;
            m_spatialAudioInitialized = true;
        }
        else
        {
            m_outputChannelMask = 0;
            m_spatialAudioInitialized = false;
        }
        m_initialized = true;
        return true;
#else
        return false;
#endif
#endif
    }

    void AudioSystem::Shutdown()
    {
        ClearEmitters();

#if defined(PLUTOGE_USE_OPENAL_SOFT)
        if (!m_availableOpenAlSources.empty())
        {
            alDeleteSources(
                static_cast<ALsizei>(m_availableOpenAlSources.size()),
                reinterpret_cast<const ALuint *>(m_availableOpenAlSources.data()));
            m_availableOpenAlSources.clear();
        }
        if (!m_availableOpenAlFilters.empty())
        {
            alDeleteFilters(
                static_cast<ALsizei>(m_availableOpenAlFilters.size()),
                reinterpret_cast<const ALuint *>(m_availableOpenAlFilters.data()));
            m_availableOpenAlFilters.clear();
        }

        for (auto &[_, clip] : m_clipCache)
        {
            const ALuint backendBuffer = static_cast<ALuint>(clip.backendBuffer);
            const ALuint monoBackendBuffer = static_cast<ALuint>(clip.monoBackendBuffer);
            if (clip.backendBuffer != 0)
            {
                alDeleteBuffers(1, &backendBuffer);
                clip.backendBuffer = 0;
            }

            if (clip.monoBackendBuffer != 0 && monoBackendBuffer != backendBuffer)
            {
                alDeleteBuffers(1, &monoBackendBuffer);
                clip.monoBackendBuffer = 0;
            }
        }

        if (auto *context = static_cast<ALCcontext *>(m_openAlContext))
        {
            alcMakeContextCurrent(nullptr);
            alcDestroyContext(context);
        }

        if (auto *device = static_cast<ALCdevice *>(m_openAlDevice))
        {
            alcCloseDevice(device);
        }
#else
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
#endif

        m_masterVoice = nullptr;
        m_xaudio = nullptr;
        m_openAlDevice = nullptr;
        m_openAlContext = nullptr;
        m_outputChannels = 2;
        m_outputChannelMask = 0;
        m_spatialAudioInitialized = false;
        m_usingOpenAl = false;
        m_openAlEfxAvailable = false;
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

#if defined(PLUTOGE_USE_OPENAL_SOFT) || defined(_WIN32)
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
        if (it->second.channels <= 1)
        {
            it->second.monoSamples = it->second.samples;
        }
        else
        {
            const std::size_t frameCount = it->second.samples.size() / static_cast<std::size_t>(it->second.channels);
            it->second.monoSamples.resize(frameCount);
            for (std::size_t frameIndex = 0; frameIndex < frameCount; ++frameIndex)
            {
                float mixedSample = 0.0f;
                for (int channelIndex = 0; channelIndex < it->second.channels; ++channelIndex)
                {
                    mixedSample += it->second.samples[frameIndex * static_cast<std::size_t>(it->second.channels) + static_cast<std::size_t>(channelIndex)];
                }

                it->second.monoSamples[frameIndex] = mixedSample / static_cast<float>(it->second.channels);
            }
        }

#if defined(PLUTOGE_USE_OPENAL_SOFT)
        ALuint buffer = 0;
        ALuint monoBuffer = 0;
        alGenBuffers(1, &buffer);
        alGenBuffers(1, &monoBuffer);
        if (buffer == 0 || monoBuffer == 0)
        {
            if (buffer != 0)
            {
                alDeleteBuffers(1, &buffer);
            }
            if (monoBuffer != 0)
            {
                alDeleteBuffers(1, &monoBuffer);
            }
            m_clipCache.erase(it);
            return false;
        }

        const auto monoOrOriginalSamples = ConvertMonoToInt16(it->second.samples);
        const auto stereoSamples = ConvertToStereoInt16(it->second.samples, it->second.channels);
        alBufferData(buffer,
                     it->second.channels <= 1 ? AL_FORMAT_MONO16 : AL_FORMAT_STEREO16,
                     it->second.channels <= 1 ? static_cast<const void *>(monoOrOriginalSamples.data()) : static_cast<const void *>(stereoSamples.data()),
                     it->second.channels <= 1 ? static_cast<ALsizei>(monoOrOriginalSamples.size() * sizeof(std::int16_t)) : static_cast<ALsizei>(stereoSamples.size() * sizeof(std::int16_t)),
                     it->second.sampleRate);

        const auto monoSamples = ConvertMonoToInt16(it->second.monoSamples);
        alBufferData(monoBuffer,
                     AL_FORMAT_MONO16,
                     monoSamples.data(),
                     static_cast<ALsizei>(monoSamples.size() * sizeof(std::int16_t)),
                     it->second.sampleRate);

        if (alGetError() != AL_NO_ERROR)
        {
            alDeleteBuffers(1, &buffer);
            alDeleteBuffers(1, &monoBuffer);
            m_clipCache.erase(it);
            return false;
        }

        it->second.backendBuffer = buffer;
        it->second.monoBackendBuffer = monoBuffer;
#endif
        clip = &it->second;
        return true;
#else
        (void)clipPath;
        clip = nullptr;
        return false;
#endif
    }

    bool AudioSystem::PreloadClip(const std::string &clipPath)
    {
        if (clipPath.empty())
            return false;
        const AudioClip *clip = nullptr;
        return EnsureClipLoaded(clipPath, clip);
    }

    void AudioSystem::PrewarmVoicePool(std::size_t voiceCount)
    {
#if defined(PLUTOGE_USE_OPENAL_SOFT)
        if (!m_usingOpenAl)
            return;

        while (m_availableOpenAlSources.size() < voiceCount)
        {
            ALuint source = 0;
            alGenSources(1, &source);
            if (source == 0)
                break;
            m_availableOpenAlSources.push_back(source);
        }

        if (m_openAlEfxAvailable)
        {
            while (m_availableOpenAlFilters.size() < voiceCount)
            {
                ALuint filter = 0;
                alGetError();
                alGenFilters(1, &filter);
                if (filter == 0 || alGetError() != AL_NO_ERROR)
                {
                    if (filter != 0)
                        alDeleteFilters(1, &filter);
                    break;
                }
                alFilteri(filter, AL_FILTER_TYPE, AL_FILTER_LOWPASS);
                alFilterf(filter, AL_LOWPASS_GAIN, 1.0f);
                alFilterf(filter, AL_LOWPASS_GAINHF, 1.0f);
                m_availableOpenAlFilters.push_back(filter);
            }
        }
#else
        (void)voiceCount;
#endif
    }

    void AudioSystem::DestroyVoice(std::uint64_t key)
    {
        const auto it = m_activeVoices.find(key);
        if (it == m_activeVoices.end())
        {
            return;
        }

#if defined(PLUTOGE_USE_OPENAL_SOFT)
        if (it->second.backendSource != 0)
        {
            const ALuint source = static_cast<ALuint>(it->second.backendSource);
            alSourceStop(source);
            alSourcei(source, AL_DIRECT_FILTER, 0);
            alSourcei(source, AL_BUFFER, 0);
            m_availableOpenAlSources.push_back(source);
        }

        if (it->second.backendFilter != 0)
        {
            const ALuint filter = static_cast<ALuint>(it->second.backendFilter);
            m_availableOpenAlFilters.push_back(filter);
        }

#else
#ifdef _WIN32
        auto *sourceVoice = static_cast<IXAudio2SourceVoice *>(it->second.sourceVoice);
        if (sourceVoice)
        {
            sourceVoice->Stop(0);
            sourceVoice->FlushSourceBuffers();
            sourceVoice->DestroyVoice();
        }
#endif
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

    void AudioSystem::UpdateVoice(ActiveVoice &voice, const ListenerState &listener, const EmitterState &emitter, float deltaTime)
    {
        constexpr float parameterUpdateInterval = 1.0f / 30.0f;
        voice.parameterUpdateAccumulator += std::max(deltaTime, 0.0f);
        if (voice.parameterUpdateAccumulator < parameterUpdateInterval)
        {
            return;
        }
        const float parameterDeltaTime = voice.parameterUpdateAccumulator;
        voice.parameterUpdateAccumulator = std::fmod(voice.parameterUpdateAccumulator, parameterUpdateInterval);

#if defined(PLUTOGE_USE_OPENAL_SOFT)
        if (m_usingOpenAl)
        {
            const ALuint source = static_cast<ALuint>(voice.backendSource);
            if (source == 0)
            {
                return;
            }

            alSourcef(source, AL_PITCH, std::clamp(emitter.pitch, 0.25f, 4.0f));
            alSourcei(source, AL_LOOPING, emitter.looping ? AL_TRUE : AL_FALSE);

            float gain = std::max(emitter.volume, 0.0f);
            const float userLowPass = std::clamp(emitter.lowPassStrength + listener.lowPassStrength, 0.0f, 1.0f);
            float filterDamping = userLowPass;
            if (emitter.spatialized && listener.active)
            {
                const float safeDeltaTime = std::max(parameterDeltaTime, 0.0001f);
                const glm::vec3 emitterVelocity = voice.hasPreviousSpatialState
                                                      ? (emitter.position - voice.previousEmitterPosition) / safeDeltaTime
                                                      : glm::vec3(0.0f);
                const float targetOcclusion = std::clamp(emitter.occlusion, 0.0f, 1.0f);
                const float distance = glm::distance(listener.position, emitter.position);
                const float attenuation = ComputeAttenuation(emitter, distance);
                const float distanceRange = (std::max)(0.001f, emitter.maxDistance - emitter.minDistance);
                const float targetAirAbsorption = std::clamp(((distance - emitter.minDistance) / distanceRange) *
                                                                 emitter.airAbsorptionStrength * listener.airAbsorptionStrength,
                                                             0.0f,
                                                             1.0f);
                voice.smoothedOcclusion += (targetOcclusion - voice.smoothedOcclusion) * 0.18f;
                voice.smoothedAirAbsorption += (targetAirAbsorption - voice.smoothedAirAbsorption) * 0.08f;

                gain *= attenuation;
                gain *= 1.0f - 0.65f * voice.smoothedOcclusion;
                filterDamping = std::clamp(userLowPass + 0.95f * voice.smoothedOcclusion + 0.25f * voice.smoothedAirAbsorption,
                                           0.0f,
                                           1.0f);

                alSourcei(source, AL_SOURCE_RELATIVE, AL_FALSE);
                alSource3f(source, AL_POSITION, emitter.position.x, emitter.position.y, emitter.position.z);
                alSource3f(source, AL_VELOCITY, emitterVelocity.x, emitterVelocity.y, emitterVelocity.z);
                alSourcef(source, AL_REFERENCE_DISTANCE, std::max(emitter.minDistance, 0.001f));
                alSourcef(source, AL_MAX_DISTANCE, std::max(emitter.maxDistance, emitter.minDistance));
                alSourcef(source, AL_ROLLOFF_FACTOR, std::max(emitter.rolloff, 0.01f));

                voice.previousEmitterPosition = emitter.position;
                voice.hasPreviousSpatialState = true;
            }
            else
            {
                voice.hasPreviousSpatialState = false;

                alSourcei(source, AL_SOURCE_RELATIVE, AL_TRUE);
                alSource3f(source, AL_POSITION, 0.0f, 0.0f, 0.0f);
                alSource3f(source, AL_VELOCITY, 0.0f, 0.0f, 0.0f);
            }

            if (voice.backendFilter != 0)
            {
                const ALuint filter = static_cast<ALuint>(voice.backendFilter);
                if (filterDamping > 0.0001f)
                {
                    const float highFrequencyGain = std::clamp(std::exp(std::log(1.0f) + (std::log(0.035f) - std::log(1.0f)) * filterDamping),
                                                               0.035f,
                                                               1.0f);
                    alFilteri(filter, AL_FILTER_TYPE, AL_FILTER_LOWPASS);
                    alFilterf(filter, AL_LOWPASS_GAIN, 1.0f);
                    alFilterf(filter, AL_LOWPASS_GAINHF, highFrequencyGain);
                    alSourcei(source, AL_DIRECT_FILTER, static_cast<ALint>(filter));
                }
                else
                {
                    alSourcei(source, AL_DIRECT_FILTER, 0);
                }
            }

            alSourcef(source, AL_GAIN, gain);
            return;
        }
#endif

#ifdef _WIN32
        auto *sourceVoice = static_cast<IXAudio2SourceVoice *>(voice.sourceVoice);
        if (!sourceVoice)
        {
            return;
        }

        sourceVoice->SetFrequencyRatio(std::clamp(emitter.pitch, 0.25f, 4.0f));

        float gain = std::max(emitter.volume, 0.0f);
        const float userLowPass = std::clamp(emitter.lowPassStrength + listener.lowPassStrength, 0.0f, 1.0f);
        float filterDamping = userLowPass;
        if (emitter.spatialized && listener.active)
        {
            const float distance = glm::distance(listener.position, emitter.position);
            const float attenuation = ComputeAttenuation(emitter, distance);
            const float distanceRange = (std::max)(0.001f, emitter.maxDistance - emitter.minDistance);
            const float targetAirAbsorption = std::clamp(((distance - emitter.minDistance) / distanceRange) *
                                                             emitter.airAbsorptionStrength * listener.airAbsorptionStrength,
                                                         0.0f,
                                                         1.0f);
            const float targetOcclusion = std::clamp(emitter.occlusion, 0.0f, 1.0f);
            voice.smoothedOcclusion += (targetOcclusion - voice.smoothedOcclusion) * 0.18f;
            voice.smoothedAirAbsorption += (targetAirAbsorption - voice.smoothedAirAbsorption) * 0.08f;

            gain *= attenuation;
            gain *= 1.0f - 0.65f * voice.smoothedOcclusion;
            gain *= std::max(listener.masterVolume, 0.0f);

            const std::size_t matrixSize =
                static_cast<std::size_t>(voice.channels) * static_cast<std::size_t>(m_outputChannels);
            voice.dspMatrix.assign(matrixSize, 0.0f);
            auto &matrix = voice.dspMatrix;
            float x3dLowPassDamping = 0.0f;
            if (m_spatialAudioInitialized && voice.channels == 1)
            {
                const float safeDeltaTime = std::max(parameterDeltaTime, 0.0001f);
                const glm::vec3 listenerVelocity = voice.hasPreviousSpatialState
                                                       ? (listener.position - voice.previousListenerPosition) / safeDeltaTime
                                                       : glm::vec3(0.0f);
                const glm::vec3 emitterVelocity = voice.hasPreviousSpatialState
                                                      ? (emitter.position - voice.previousEmitterPosition) / safeDeltaTime
                                                      : glm::vec3(0.0f);

                X3DAUDIO_LISTENER x3dListener{};
                x3dListener.Position = ToX3DAudioVector(listener.position);
                x3dListener.OrientFront = ToX3DAudioVector(SafeNormalize(listener.forward, glm::vec3(0.0f, 0.0f, -1.0f)));
                x3dListener.OrientTop = ToX3DAudioVector(SafeNormalize(listener.up, glm::vec3(0.0f, 1.0f, 0.0f)));
                x3dListener.Velocity = ToX3DAudioVector(listenerVelocity);

                X3DAUDIO_EMITTER x3dEmitter{};
                X3DAUDIO_DISTANCE_CURVE_POINT flatVolumeCurvePoints[2]{{0.0f, 1.0f}, {1.0f, 1.0f}};
                X3DAUDIO_DISTANCE_CURVE flatVolumeCurve{flatVolumeCurvePoints, 2};
                x3dEmitter.ChannelCount = 1;
                x3dEmitter.CurveDistanceScaler = std::max(emitter.maxDistance, 0.001f);
                x3dEmitter.Position = ToX3DAudioVector(emitter.position);
                x3dEmitter.OrientFront = ToX3DAudioVector(SafeNormalize(emitter.position - listener.position, glm::vec3(0.0f, 0.0f, -1.0f)));
                x3dEmitter.OrientTop = ToX3DAudioVector(glm::vec3(0.0f, 1.0f, 0.0f));
                x3dEmitter.Velocity = ToX3DAudioVector(emitterVelocity);
                x3dEmitter.pVolumeCurve = &flatVolumeCurve;

                X3DAUDIO_DSP_SETTINGS dspSettings{};
                dspSettings.SrcChannelCount = 1;
                dspSettings.DstChannelCount = m_outputChannels;
                dspSettings.pMatrixCoefficients = matrix.data();

                X3DAudioCalculate(reinterpret_cast<const BYTE *>(m_spatialAudioHandle.data()),
                                  &x3dListener,
                                  &x3dEmitter,
                                  X3DAUDIO_CALCULATE_MATRIX | X3DAUDIO_CALCULATE_DOPPLER | X3DAUDIO_CALCULATE_LPF_DIRECT,
                                  &dspSettings);

                for (float &coefficient : matrix)
                {
                    coefficient *= gain;
                }

                const float frequencyRatio = std::clamp(emitter.pitch * dspSettings.DopplerFactor, XAUDIO2_MIN_FREQ_RATIO, XAUDIO2_MAX_FREQ_RATIO);
                sourceVoice->SetFrequencyRatio(frequencyRatio);
                x3dLowPassDamping = std::clamp(1.0f - dspSettings.LPFDirectCoefficient, 0.0f, 1.0f);
            }
            else if (m_outputChannels >= 2 && voice.channels == 1)
            {
                const float pan = ComputePan(listener, emitter);
                matrix[0] = gain * (pan <= 0.0f ? 1.0f : 1.0f - pan);
                matrix[1] = gain * (pan >= 0.0f ? 1.0f : 1.0f + pan);
            }
            else
            {
                for (int sourceChannel = 0; sourceChannel < voice.channels; ++sourceChannel)
                {
                    const unsigned int targetChannel = std::min(static_cast<unsigned int>(sourceChannel), m_outputChannels - 1);
                    matrix[static_cast<std::size_t>(sourceChannel) * static_cast<std::size_t>(m_outputChannels) + targetChannel] = gain;
                }
            }

            sourceVoice->SetVolume(1.0f);
            sourceVoice->SetOutputMatrix(static_cast<IXAudio2Voice *>(m_masterVoice), voice.channels, m_outputChannels, matrix.data());
            voice.previousListenerPosition = listener.position;
            voice.previousEmitterPosition = emitter.position;
            voice.hasPreviousSpatialState = true;

            XAUDIO2_FILTER_PARAMETERS filterParameters{};
            filterParameters.Type = LowPassFilter;
            filterParameters.OneOverQ = 1.0f;
            filterDamping = std::clamp(userLowPass + voice.smoothedOcclusion + x3dLowPassDamping + 0.25f * voice.smoothedAirAbsorption,
                                       0.0f,
                                       1.0f);
            const float cutoffHz = InterpolateLogFrequency(18000.0f, 450.0f, filterDamping);
            filterParameters.Frequency = ToXAudioFilterFrequency(cutoffHz, voice.sampleRate);
            sourceVoice->SetFilterParameters(&filterParameters);
            return;
        }

        gain *= listener.active ? std::max(listener.masterVolume, 0.0f) : 1.0f;
        sourceVoice->SetVolume(gain);

        const std::size_t matrixSize =
            static_cast<std::size_t>(voice.channels) * static_cast<std::size_t>(m_outputChannels);
        voice.dspMatrix.assign(matrixSize, 0.0f);
        auto &identityMatrix = voice.dspMatrix;
        for (int sourceChannel = 0; sourceChannel < voice.channels; ++sourceChannel)
        {
            const unsigned int targetChannel = std::min(static_cast<unsigned int>(sourceChannel), m_outputChannels - 1);
            identityMatrix[static_cast<std::size_t>(sourceChannel) * static_cast<std::size_t>(m_outputChannels) + targetChannel] = 1.0f;
        }
        sourceVoice->SetOutputMatrix(static_cast<IXAudio2Voice *>(m_masterVoice), voice.channels, m_outputChannels, identityMatrix.data());
        voice.hasPreviousSpatialState = false;

        XAUDIO2_FILTER_PARAMETERS filterParameters{};
        filterParameters.Type = LowPassFilter;
        filterParameters.OneOverQ = 1.0f;
        const float cutoffHz = InterpolateLogFrequency(18000.0f, 450.0f, filterDamping);
        filterParameters.Frequency = ToXAudioFilterFrequency(cutoffHz, voice.sampleRate);
        sourceVoice->SetFilterParameters(&filterParameters);
#else
        (void)voice;
        (void)listener;
        (void)emitter;
#endif
    }

    void AudioSystem::StopInactiveEmitters(const std::vector<std::uint64_t> &activeKeys)
    {
        const std::unordered_set<std::uint64_t> activeKeySet(activeKeys.begin(), activeKeys.end());
        std::vector<std::uint64_t> staleKeys;
        staleKeys.reserve(m_activeVoices.size());
        for (const auto &[key, _] : m_activeVoices)
        {
            if (!activeKeySet.contains(key))
            {
                staleKeys.push_back(key);
            }
        }

        for (const auto key : staleKeys)
        {
            DestroyVoice(key);
        }
    }

    void AudioSystem::Update(const ListenerState &listener, const std::vector<EmitterState> &emitters, float deltaTime)
    {
#ifndef _WIN32
#if !defined(PLUTOGE_USE_OPENAL_SOFT)
        (void)listener;
        (void)emitters;
        (void)deltaTime;
        return;
#endif
#else
        if (!m_initialized)
        {
            return;
        }
#endif

        // Keep backend mixing cost bounded. Sounds which cannot currently make
        // an audible contribution are the first candidates for voice stealing.
        constexpr std::size_t maximumMixedVoiceCount = 64;
        std::unordered_set<std::uint64_t> mixedKeys;
        mixedKeys.reserve(std::min(emitters.size(), maximumMixedVoiceCount));
        if (emitters.size() <= maximumMixedVoiceCount)
        {
            for (const auto &emitter : emitters)
                mixedKeys.insert(emitter.key);
        }
        else
        {
            std::vector<std::pair<float, std::uint64_t>> priorities;
            priorities.reserve(emitters.size());
            for (const auto &emitter : emitters)
            {
                float priority = std::max(emitter.volume, 0.0f);
                if (!emitter.spatialized || !listener.active)
                {
                    priority += 1000.0f;
                }
                else
                {
                    const float distance = glm::distance(listener.position, emitter.position);
                    const float range = std::max(emitter.maxDistance, 0.001f);
                    priority *= std::clamp(1.0f - distance / range, 0.0f, 1.0f);
                }
                if (emitter.looping)
                    priority += 0.001f;
                priorities.emplace_back(priority, emitter.key);
            }
            std::partial_sort(
                priorities.begin(),
                priorities.begin() + static_cast<std::ptrdiff_t>(maximumMixedVoiceCount),
                priorities.end(),
                [](const auto &left, const auto &right)
                {
                    return left.first > right.first;
                });
            for (std::size_t index = 0; index < maximumMixedVoiceCount; ++index)
                mixedKeys.insert(priorities[index].second);
        }

#if defined(PLUTOGE_USE_OPENAL_SOFT)
        if (m_usingOpenAl)
        {
            const float safeDeltaTime = std::max(deltaTime, 0.0001f);
            glm::vec3 listenerVelocity{0.0f};
            if (m_hasPreviousOpenAlListenerPosition)
            {
                listenerVelocity = (listener.position - m_previousOpenAlListenerPosition) / safeDeltaTime;
            }

            if (listener.active)
            {
                const glm::vec3 forward = SafeNormalize(listener.forward, glm::vec3(0.0f, 0.0f, -1.0f));
                const glm::vec3 up = SafeNormalize(listener.up, glm::vec3(0.0f, 1.0f, 0.0f));
                const ALfloat orientation[6]{forward.x, forward.y, forward.z, up.x, up.y, up.z};
                alListener3f(AL_POSITION, listener.position.x, listener.position.y, listener.position.z);
                alListener3f(AL_VELOCITY, listenerVelocity.x, listenerVelocity.y, listenerVelocity.z);
                alListenerfv(AL_ORIENTATION, orientation);
                alListenerf(AL_GAIN, std::max(listener.masterVolume, 0.0f));
                m_previousOpenAlListenerPosition = listener.position;
                m_hasPreviousOpenAlListenerPosition = true;
            }
            else
            {
                alListener3f(AL_POSITION, 0.0f, 0.0f, 0.0f);
                alListener3f(AL_VELOCITY, 0.0f, 0.0f, 0.0f);
                alListenerf(AL_GAIN, 1.0f);
                m_hasPreviousOpenAlListenerPosition = false;
            }

            std::vector<std::uint64_t> activeKeys;
            activeKeys.reserve(emitters.size());

            for (const auto &emitter : emitters)
            {
                activeKeys.push_back(emitter.key);

                if (!mixedKeys.contains(emitter.key))
                {
                    DestroyVoice(emitter.key);
                    continue;
                }

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
                const bool useSpatialVoice = emitter.spatialized && listener.active;
                const bool needsNewVoice = currentVoice == m_activeVoices.end() ||
                                           currentVoice->second.clipPath != emitter.clipPath ||
                                           currentVoice->second.looping != emitter.looping ||
                                           currentVoice->second.spatialized != emitter.spatialized ||
                                           currentVoice->second.usingSpatialPlayback != useSpatialVoice ||
                                           emitter.restartRequested;

                if (needsNewVoice)
                {
                    DestroyVoice(emitter.key);

                    ALuint source = 0;
                    if (!m_availableOpenAlSources.empty())
                    {
                        source = static_cast<ALuint>(m_availableOpenAlSources.back());
                        m_availableOpenAlSources.pop_back();
                    }
                    else
                    {
                        alGenSources(1, &source);
                    }
                    if (source == 0)
                    {
                        continue;
                    }

                    const ALuint buffer = useSpatialVoice ? static_cast<ALuint>(clip->monoBackendBuffer)
                                                          : static_cast<ALuint>(clip->backendBuffer);
                    alSourcei(source, AL_BUFFER, static_cast<ALint>(buffer));
                    alSourcei(source, AL_LOOPING, emitter.looping ? AL_TRUE : AL_FALSE);

                    ActiveVoice newVoice{};
                    newVoice.backendSource = source;
                    newVoice.clipPath = emitter.clipPath;
                    newVoice.channels = useSpatialVoice ? 1 : clip->channels;
                    newVoice.sampleRate = clip->sampleRate;
                    newVoice.paused = emitter.paused;
                    newVoice.looping = emitter.looping;
                    newVoice.spatialized = emitter.spatialized;
                    newVoice.usingSpatialPlayback = useSpatialVoice;
                    newVoice.smoothedOcclusion = std::clamp(emitter.occlusion, 0.0f, 1.0f);
                    newVoice.smoothedAirAbsorption = std::clamp(emitter.lowPassStrength + listener.lowPassStrength,
                                                                0.0f,
                                                                1.0f);

                    if (m_openAlEfxAvailable)
                    {
                        ALuint filter = 0;
                        bool filterCreated = false;
                        if (!m_availableOpenAlFilters.empty())
                        {
                            filter = static_cast<ALuint>(m_availableOpenAlFilters.back());
                            m_availableOpenAlFilters.pop_back();
                            filterCreated = true;
                        }
                        else
                        {
                            alGetError();
                            alGenFilters(1, &filter);
                            filterCreated = filter != 0 && alGetError() == AL_NO_ERROR;
                        }
                        if (filterCreated)
                        {
                            alFilteri(filter, AL_FILTER_TYPE, AL_FILTER_LOWPASS);
                            alFilterf(filter, AL_LOWPASS_GAIN, 1.0f);
                            alFilterf(filter, AL_LOWPASS_GAINHF, 1.0f);
                            if (alGetError() == AL_NO_ERROR)
                            {
                                newVoice.backendFilter = filter;
                            }
                            else
                            {
                                alDeleteFilters(1, &filter);
                            }
                        }
                        else if (filter != 0)
                        {
                            alDeleteFilters(1, &filter);
                        }
                    }

                    currentVoice = m_activeVoices.emplace(emitter.key, std::move(newVoice)).first;

                    if (!emitter.paused)
                    {
                        alSourcePlay(source);
                    }
                }
                else
                {
                    const ALuint source = static_cast<ALuint>(currentVoice->second.backendSource);
                    if (emitter.paused && !currentVoice->second.paused)
                    {
                        alSourcePause(source);
                        currentVoice->second.paused = true;
                    }
                    else if (!emitter.paused && currentVoice->second.paused)
                    {
                        alSourcePlay(source);
                        currentVoice->second.paused = false;
                    }

                    if (!currentVoice->second.looping)
                    {
                        ALint state = AL_INITIAL;
                        alGetSourcei(source, AL_SOURCE_STATE, &state);
                        if (state == AL_STOPPED)
                        {
                            DestroyVoice(emitter.key);
                            continue;
                        }
                    }
                }

                if (auto activeVoice = m_activeVoices.find(emitter.key); activeVoice != m_activeVoices.end())
                {
                    UpdateVoice(activeVoice->second, listener, emitter, deltaTime);
                }
            }

            StopInactiveEmitters(activeKeys);
            return;
        }
#endif

#ifdef _WIN32
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

            if (!mixedKeys.contains(emitter.key))
            {
                DestroyVoice(emitter.key);
                continue;
            }

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
                                       currentVoice->second.spatialized != emitter.spatialized ||
                                       emitter.restartRequested;

            if (needsNewVoice)
            {
                DestroyVoice(emitter.key);

                const bool useSpatialVoice = emitter.spatialized && listener.active;
                ActiveVoice newVoice{};
                newVoice.clipPath = emitter.clipPath;
                newVoice.channels = useSpatialVoice ? 1 : clip->channels;
                newVoice.sampleRate = clip->sampleRate;
                newVoice.paused = emitter.paused;
                newVoice.looping = emitter.looping;
                newVoice.spatialized = emitter.spatialized;
                newVoice.smoothedOcclusion = std::clamp(emitter.occlusion, 0.0f, 1.0f);

                const int sourceChannels = useSpatialVoice ? 1 : clip->channels;
                const std::vector<float> *sourceSamples = useSpatialVoice ? &clip->monoSamples : &clip->samples;

                WAVEFORMATEX waveFormat{};
                waveFormat.wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
                waveFormat.nChannels = static_cast<WORD>(sourceChannels);
                waveFormat.nSamplesPerSec = static_cast<DWORD>(clip->sampleRate);
                waveFormat.wBitsPerSample = 32;
                waveFormat.nBlockAlign = static_cast<WORD>(sourceChannels * sizeof(float));
                waveFormat.nAvgBytesPerSec = waveFormat.nSamplesPerSec * waveFormat.nBlockAlign;

                IXAudio2SourceVoice *sourceVoice = nullptr;
                if (FAILED(xaudio->CreateSourceVoice(&sourceVoice, &waveFormat, XAUDIO2_VOICE_USEFILTER)) || !sourceVoice)
                {
                    continue;
                }

                newVoice.sourceVoice = sourceVoice;
                currentVoice = m_activeVoices.emplace(emitter.key, std::move(newVoice)).first;
                XAUDIO2_BUFFER buffer{};
                buffer.AudioBytes = static_cast<UINT32>(sourceSamples->size() * sizeof(float));
                buffer.pAudioData = reinterpret_cast<const BYTE *>(sourceSamples->data());
                buffer.Flags = XAUDIO2_END_OF_STREAM;
                buffer.LoopCount = emitter.looping ? XAUDIO2_LOOP_INFINITE : 0;
                if (FAILED(sourceVoice->SubmitSourceBuffer(&buffer)))
                {
                    DestroyVoice(emitter.key);
                    continue;
                }

                if (!emitter.paused)
                {
                    sourceVoice->Start(0);
                }
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
                UpdateVoice(activeVoice->second, listener, emitter, deltaTime);
            }
        }

        StopInactiveEmitters(activeKeys);
#endif
    }
}
