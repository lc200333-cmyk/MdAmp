#pragma once
#include <array>
#include <cstddef>
#include <memory>
#include <string>

namespace mdamp {
struct AudioSnapshot {
    double position = 0;
    double duration = 0;
    bool playing = false;
    bool ended = false;
    std::array<float, 32> spectrum{};
    std::wstring format;
};

class AudioEngine {
public:
    AudioEngine();
    ~AudioEngine();
    AudioEngine(const AudioEngine&) = delete;
    AudioEngine& operator=(const AudioEngine&) = delete;
    bool Open(const std::wstring& path, std::wstring& error);
    void Close();
    bool Play();
    void Pause();
    void Stop();
    void Seek(double seconds);
    void SetVolume(float volume);
    void SetPan(float pan);
    void SetEqualizer(bool enabled, const std::array<float, 10>& gainsDb, float preampDb);
    void SetDspCallback(void (*callback)(float*, std::size_t, void*), void* user);
    AudioSnapshot Snapshot() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
