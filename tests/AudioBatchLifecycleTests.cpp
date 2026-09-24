#include "PlutoGE/audio/AudioSystem.h"
#include <AL/al.h>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>

void Check(bool value, const char *message) { if (!value) throw std::runtime_error(message); }
int main() try
{
    using namespace PlutoGE::audio;
    const auto path = std::filesystem::temp_directory_path() /
        ("PlutoGE-audio-batch-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".wav");
    struct Remove { std::filesystem::path path; ~Remove() { std::error_code error; std::filesystem::remove(path, error); } } cleanup{path};
    {
        std::ofstream file(path, std::ios::binary);
        const auto write = [&](std::uint32_t value, int bytes) { for (int i = 0; i < bytes; ++i) file.put(static_cast<char>((value >> (i * 8)) & 255)); };
        file.write("RIFF", 4); write(36 + 96000, 4); file.write("WAVEfmt ", 8);
        write(16, 4); write(1, 2); write(1, 2); write(48000, 4); write(96000, 4); write(2, 2); write(16, 2);
        file.write("data", 4); write(96000, 4); for (int i = 0; i < 48000; ++i) write(0, 2);
    }
    AudioSystem audio;
    Check(audio.Initialize(), "Null audio device initialization failed");
    struct Shutdown { AudioSystem &audio; ~Shutdown() { audio.Shutdown(); } } shutdown{audio};
    Check(audio.PreloadClip(path.string()), "Clip preparation failed");
    audio.PrewarmVoicePool(64);
    ListenerState listener; listener.active = true;
    std::vector<EmitterState> emitters(16);
    for (std::size_t i = 0; i < emitters.size(); ++i)
    { emitters[i].key = i + 1; emitters[i].clipPath = path.string(); emitters[i].playing = true; emitters[i].looping = true; emitters[i].spatialized = false; }
    audio.Update(listener, emitters, .016f);
    for (const auto &emitter : emitters) Check(audio.IsEmitterActive(emitter.key), "Burst voice missing");
    for (auto &emitter : emitters) emitter.paused = true;
    audio.Update(listener, emitters, .016f);
    for (auto &emitter : emitters) emitter.paused = false;
    emitters[0].restartRequested = true;
    emitters[1].spatialized = true;
    emitters[2].looping = false;
    emitters[3].playing = false;
    emitters.resize(8);
    audio.Update(listener, emitters, .016f);
    Check(!audio.IsEmitterActive(4) && !audio.IsEmitterActive(16), "Retired voices retained");
    Check(audio.IsEmitterActive(1) && audio.IsEmitterActive(2), "Restart/spatial transition failed");
    emitters[0].restartRequested = false;
    // Simulate very slow frames and a clamped simulation delta. Playback must
    // follow the audio device clock, not accumulated game Update time.
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    audio.Update(listener, emitters, .001f);
    Check(audio.IsEmitterActive(3), "One-shot completed before its audio duration");
    std::this_thread::sleep_for(std::chrono::milliseconds(900));
    emitters[2].paused = true; // Completion and pause in the same update must not leave a queued stale handle.
    audio.Update(listener, emitters, .016f);
    Check(!audio.IsEmitterActive(3), "Completed one-shot retained");
    Check(audio.IsEmitterActive(1) && audio.IsEmitterActive(2), "Loop stopped during slow frames");
    Check(alGetError() == AL_NO_ERROR, "Invalid OpenAL batch lifecycle operation");
    audio.Update(listener, {}, .016f);
    Check(!audio.IsEmitterActive(1), "Empty snapshot did not retire playback");
    Check(alGetError() == AL_NO_ERROR, "Audio cleanup error");
    std::cout << "PASS: burst start, pause/resume, restart, spatial/loop changes, retirement and device-clock playback across slow frames\n";
}
catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
