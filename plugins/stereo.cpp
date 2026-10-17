#include "plugin_api.h"
#include <algorithm>
#include <atomic>
namespace {
std::atomic<float> width{0.0f};
void SetAmount(float amount) { width.store(std::clamp(amount, 0.0f, 1.0f)); }
void Process(float* samples, std::uint32_t frames, std::uint32_t channels) {
    if (!samples || channels != 2) return;
    const float factor = width.load();
    if (factor <= 0) return;
    for (std::uint32_t i = 0; i < frames; ++i) {
        const auto offset = static_cast<std::size_t>(i) * 2;
        const float mid = (samples[offset] + samples[offset + 1]) * .5f;
        const float side = (samples[offset] - samples[offset + 1]) * (.5f + factor * .5f);
        samples[offset] = std::clamp(mid + side, -1.0f, 1.0f);
        samples[offset + 1] = std::clamp(mid - side, -1.0f, 1.0f);
    }
}
const MdAmpDspV1 api{1, L"MdAmp Stereo Enhancer", Process, SetAmount};
}
extern "C" __declspec(dllexport) const MdAmpDspV1* MdAmpGetPluginV1() { return &api; }
