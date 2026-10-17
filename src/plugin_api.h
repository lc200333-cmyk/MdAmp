#pragma once
#include <cstdint>
// MdAmp-specific ABI. Not the Winamp input/output/DSP ABI.
struct MdAmpDspV1 {
    std::uint32_t abiVersion;
    const wchar_t* name;
    void (*process)(float* interleaved, std::uint32_t frames, std::uint32_t channels);
    void (*setAmount)(float amount);
};
using MdAmpGetDspV1 = const MdAmpDspV1* (*)();
