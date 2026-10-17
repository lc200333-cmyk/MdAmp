#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace mdamp {
// Independently implemented constant-Q peaking filters (RBJ biquad equations).
// Each stereo channel has independent delay state. A disabled EQ is a true bypass.
class Equalizer {
    struct Filter {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
        std::array<double, 2> z1{}, z2{};
        float Tick(float input, std::size_t channel) {
            const double output = b0 * input + z1[channel];
            z1[channel] = b1 * input - a1 * output + z2[channel];
            z2[channel] = b2 * input - a2 * output;
            return static_cast<float>(output);
        }
    };
    std::array<Filter, 10> filters_{};
    bool enabled_ = false;
    float preamp_ = 1;
public:
    inline static constexpr std::array<float, 10> Frequencies{60, 170, 310, 600, 1000, 3000, 6000, 12000, 14000, 16000};
    void Reset() { for (auto& f : filters_) { f.z1 = {}; f.z2 = {}; } }
    void Configure(bool enabled, const std::array<float, 10>& gains, float preampDb) {
        enabled_ = enabled;
        preamp_ = std::pow(10.0f, std::clamp(std::isfinite(preampDb) ? preampDb : 0.0f, -20.0f, 20.0f) / 20.0f);
        constexpr double pi = 3.14159265358979323846;
        for (std::size_t i = 0; i < filters_.size(); ++i) {
            const double gain = std::clamp(std::isfinite(gains[i]) ? gains[i] : 0.0f, -20.0f, 20.0f);
            const double a = std::pow(10.0, gain / 40.0);
            const double w = 2 * pi * Frequencies[i] / 48000.0;
            const double alpha = std::sin(w) / (2 * 1.4);
            const double divisor = 1 + alpha / a;
            auto& f = filters_[i];
            f.b0 = (1 + alpha * a) / divisor;
            f.b1 = -2 * std::cos(w) / divisor;
            f.b2 = (1 - alpha * a) / divisor;
            f.a1 = f.b1;
            f.a2 = (1 - alpha / a) / divisor;
        }
        Reset();
    }
    void Process(float* samples, std::size_t frames) {
        if (!enabled_) return;
        for (std::size_t frame = 0; frame < frames; ++frame) {
            for (std::size_t channel = 0; channel < 2; ++channel) {
                float value = samples[frame * 2 + channel] * preamp_;
                for (auto& filter : filters_) value = filter.Tick(value, channel);
                samples[frame * 2 + channel] = std::isfinite(value) ? value : 0;
            }
        }
    }
};
}
