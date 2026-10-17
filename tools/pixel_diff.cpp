// Strict comparison of two decoded PNG frames, not proof of application parity.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <wincodec.h>
#include <bcrypt.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using Microsoft::WRL::ComPtr;
using Bytes = std::vector<BYTE>;
constexpr std::uint64_t MaxFileBytes = 128ULL * 1024 * 1024;
constexpr std::uint64_t MaxPixels = 16777216;

void Check(HRESULT result, const char* operation) {
    if (FAILED(result)) {
        std::ostringstream out;
        out << operation << " failed (HRESULT 0x" << std::hex << static_cast<unsigned long>(result) << ")";
        throw std::runtime_error(out.str());
    }
}
void CheckWin(bool result, const char* operation) {
    if (!result) throw std::runtime_error(std::string(operation) + " failed (Win32 " + std::to_string(GetLastError()) + ")");
}
struct Handle {
    HANDLE value = INVALID_HANDLE_VALUE;
    explicit Handle(HANDLE h) : value(h) {}
    ~Handle() { if (value != INVALID_HANDLE_VALUE) CloseHandle(value); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
};
struct Image {
    UINT width = 0, height = 0;
    Bytes pixels;
    std::string sha256;
};

std::string Hash(const Bytes& data) {
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0)
        throw std::runtime_error("BCrypt SHA-256 provider unavailable");
    struct CloseAlgorithm {
        BCRYPT_ALG_HANDLE handle;
        ~CloseAlgorithm() { BCryptCloseAlgorithmProvider(handle, 0); }
    } close{algorithm};
    std::array<BYTE, 32> digest{};
    if (BCryptHash(algorithm, nullptr, 0, const_cast<PUCHAR>(data.data()),
        static_cast<ULONG>(data.size()), digest.data(), static_cast<ULONG>(digest.size())) < 0)
        throw std::runtime_error("BCrypt SHA-256 hashing failed");
    std::ostringstream out;
    out << std::hex << std::setfill('0');
    for (BYTE value : digest) out << std::setw(2) << static_cast<unsigned int>(value);
    return out.str();
}

Bytes ReadPng(const std::filesystem::path& path) {
    Handle file(CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr));
    CheckWin(file.value != INVALID_HANDLE_VALUE, "Open input");
    LARGE_INTEGER size{};
    CheckWin(GetFileSizeEx(file.value, &size) != FALSE, "Read input size");
    if (size.QuadPart < 24 || static_cast<std::uint64_t>(size.QuadPart) > MaxFileBytes)
        throw std::runtime_error("Input must be a PNG of 24 bytes to 128 MiB");
    Bytes bytes(static_cast<std::size_t>(size.QuadPart));
    DWORD count = 0;
    CheckWin(ReadFile(file.value, bytes.data(), static_cast<DWORD>(bytes.size()), &count, nullptr) != FALSE,
        "Read input");
    if (count != bytes.size()) throw std::runtime_error("Incomplete input read");
    constexpr BYTE signature[]{137, 80, 78, 71, 13, 10, 26, 10};
    if (std::memcmp(bytes.data(), signature, sizeof(signature)) != 0 ||
        std::memcmp(bytes.data() + 12, "IHDR", 4) != 0)
        throw std::runtime_error("Input is not a PNG with an IHDR header");
    const auto be32 = [&bytes](std::size_t offset) {
        return (static_cast<std::uint32_t>(bytes[offset]) << 24) |
            (static_cast<std::uint32_t>(bytes[offset + 1]) << 16) |
            (static_cast<std::uint32_t>(bytes[offset + 2]) << 8) | bytes[offset + 3];
    };
    const std::uint64_t width = be32(16), height = be32(20);
    if (!width || !height || width * height > MaxPixels)
        throw std::runtime_error("PNG exceeds the 16,777,216-pixel limit or has zero dimensions");
    return bytes;
}

Image Decode(IWICImagingFactory* factory, const std::filesystem::path& path) {
    Bytes bytes = ReadPng(path);
    Image image;
    image.sha256 = Hash(bytes);
    ComPtr<IWICStream> stream;
    Check(factory->CreateStream(&stream), "Create PNG stream");
    Check(stream->InitializeFromMemory(bytes.data(), static_cast<DWORD>(bytes.size())), "Initialize PNG stream");
    ComPtr<IWICBitmapDecoder> decoder;
    Check(factory->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnDemand, &decoder), "Decode PNG");
    GUID container{};
    Check(decoder->GetContainerFormat(&container), "Read container format");
    if (!IsEqualGUID(container, GUID_ContainerFormatPng)) throw std::runtime_error("Only PNG is supported");
    UINT frames = 0;
    Check(decoder->GetFrameCount(&frames), "Read frame count");
    if (frames != 1) throw std::runtime_error("Only single decoded PNG frames are supported");
    ComPtr<IWICBitmapFrameDecode> frame;
    Check(decoder->GetFrame(0, &frame), "Read PNG frame");
    Check(frame->GetSize(&image.width, &image.height), "Read PNG dimensions");
    const std::uint64_t pixels = static_cast<std::uint64_t>(image.width) * image.height;
    if (!image.width || !image.height || pixels > MaxPixels ||
        static_cast<std::uint64_t>(image.width) * 4 > std::numeric_limits<UINT>::max() ||
        pixels * 4 > std::numeric_limits<UINT>::max())
        throw std::runtime_error("Decoded dimensions exceed bounded BGRA limits");
    ComPtr<IWICFormatConverter> converter;
    Check(factory->CreateFormatConverter(&converter), "Create BGRA converter");
    Check(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppBGRA,
        WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom), "Convert to straight BGRA");
    image.pixels.resize(static_cast<std::size_t>(pixels * 4));
    Check(converter->CopyPixels(nullptr, image.width * 4, static_cast<UINT>(image.pixels.size()),
        image.pixels.data()), "Read BGRA pixels");
    return image;
}

void WriteNew(const std::filesystem::path& path, const BYTE* bytes, std::size_t size) {
    if (size > std::numeric_limits<DWORD>::max()) throw std::runtime_error("Output size overflow");
    Handle file(CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr));
    CheckWin(file.value != INVALID_HANDLE_VALUE, "Create new output file");
    DWORD written = 0;
    CheckWin(WriteFile(file.value, bytes, static_cast<DWORD>(size), &written, nullptr) != FALSE, "Write output");
    if (written != size) throw std::runtime_error("Incomplete output write");
}

void SavePng(IWICImagingFactory* factory, const std::filesystem::path& path, const Image& image) {
    ComPtr<IStream> stream;
    Check(CreateStreamOnHGlobal(nullptr, TRUE, &stream), "Create PNG output stream");
    ComPtr<IWICBitmapEncoder> encoder;
    Check(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder), "Create PNG encoder");
    Check(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache), "Initialize PNG encoder");
    ComPtr<IWICBitmapFrameEncode> frame;
    ComPtr<IPropertyBag2> properties;
    Check(encoder->CreateNewFrame(&frame, &properties), "Create PNG output frame");
    Check(frame->Initialize(properties.Get()), "Initialize PNG output frame");
    Check(frame->SetSize(image.width, image.height), "Set PNG output size");
    WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
    Check(frame->SetPixelFormat(&format), "Set PNG output format");
    if (!IsEqualGUID(format, GUID_WICPixelFormat32bppBGRA)) throw std::runtime_error("PNG encoder changed BGRA format");
    Check(frame->WritePixels(image.height, image.width * 4, static_cast<UINT>(image.pixels.size()),
        const_cast<BYTE*>(image.pixels.data())), "Write PNG pixels");
    Check(frame->Commit(), "Commit PNG frame");
    Check(encoder->Commit(), "Commit PNG output");
    STATSTG stat{};
    Check(stream->Stat(&stat, STATFLAG_NONAME), "Read encoded PNG size");
    if (stat.cbSize.QuadPart > MaxFileBytes) throw std::runtime_error("Encoded PNG exceeds output limit");
    HGLOBAL memory = nullptr;
    Check(GetHGlobalFromStream(stream.Get(), &memory), "Read PNG output memory");
    const void* data = GlobalLock(memory);
    if (!data) throw std::runtime_error("Cannot lock PNG output memory");
    try { WriteNew(path, static_cast<const BYTE*>(data), static_cast<std::size_t>(stat.cbSize.QuadPart)); }
    catch (...) { GlobalUnlock(memory); throw; }
    GlobalUnlock(memory);
}

int Compare(IWICImagingFactory* factory, const std::filesystem::path& reference,
    const std::filesystem::path& candidate, const std::filesystem::path& output) {
    // Decode and hash both complete inputs before creating any output.
    const Image first = Decode(factory, reference), second = Decode(factory, candidate);
    if (GetFileAttributesW(output.c_str()) != INVALID_FILE_ATTRIBUTES)
        throw std::runtime_error("Output directory must not already exist");
    const bool dimensionsEqual = first.width == second.width && first.height == second.height;
    std::uint64_t changed = 0, sum = 0;
    unsigned int maximum = 0;
    Image heatmap;
    if (dimensionsEqual) {
        heatmap.width = first.width; heatmap.height = first.height;
        heatmap.pixels.resize(first.pixels.size());
        for (std::size_t i = 0; i < first.pixels.size(); i += 4) {
            bool differs = false;
            for (std::size_t channel = 0; channel < 4; ++channel) {
                const unsigned int delta = static_cast<unsigned int>(std::abs(
                    static_cast<int>(first.pixels[i + channel]) - static_cast<int>(second.pixels[i + channel])));
                differs = differs || delta != 0; maximum = std::max(maximum, delta); sum += delta;
            }
            if (differs) ++changed;
            heatmap.pixels[i] = 0; heatmap.pixels[i + 1] = 0;
            heatmap.pixels[i + 2] = differs ? 255 : 0; heatmap.pixels[i + 3] = 255;
        }
    }
    const bool equal = dimensionsEqual && changed == 0;
    std::ostringstream report;
    report << std::setprecision(17)
        << "{\n  \"schema_version\": 1,\n  \"comparison\": \"decoded PNG straight BGRA, all four channels, exact\",\n"
        << "  \"reference\": {\"width\": " << first.width << ", \"height\": " << first.height
        << ", \"file_sha256\": \"" << first.sha256 << "\"},\n"
        << "  \"candidate\": {\"width\": " << second.width << ", \"height\": " << second.height
        << ", \"file_sha256\": \"" << second.sha256 << "\"},\n"
        << "  \"dimensions_equal\": " << (dimensionsEqual ? "true" : "false") << ",\n"
        << "  \"changed_pixel_count\": ";
    if (dimensionsEqual) report << changed; else report << "null";
    report << ",\n  \"max_channel_delta\": ";
    if (dimensionsEqual) report << maximum; else report << "null";
    report << ",\n  \"mean_absolute_channel_delta\": ";
    if (dimensionsEqual) report << static_cast<double>(sum) / static_cast<double>(first.pixels.size());
    else report << "null";
    report << ",\n  \"pair_pixel_equal\": " << (equal ? "true" : "false")
        << ",\n  \"screenshot_provenance_verified\": false,\n"
        << "  \"full_application_equivalence_verified\": false,\n  \"heatmap\": "
        << (dimensionsEqual ? "\"heatmap.png\"" : "null") << "\n}\n";
    CheckWin(CreateDirectoryW(output.c_str(), nullptr) != FALSE, "Create NEW output directory");
    if (dimensionsEqual) SavePng(factory, output / L"heatmap.png", heatmap);
    const std::string json = report.str();
    WriteNew(output / L"report.json", reinterpret_cast<const BYTE*>(json.data()), json.size());
    std::cout << (equal ? "Exact decoded pixel pair match" : dimensionsEqual ? "Pixel difference" : "Dimension mismatch")
        << "; screenshot provenance and full application equivalence are NOT verified.\n";
    return equal ? 0 : 1;
}

int SelfTest(IWICImagingFactory* factory) {
    wchar_t temp[MAX_PATH + 1]{};
    const DWORD length = GetTempPathW(MAX_PATH, temp);
    if (!length || length > MAX_PATH) throw std::runtime_error("Cannot obtain temporary directory");
    GUID guid{}; Check(CoCreateGuid(&guid), "Create self-test identifier");
    wchar_t identifier[40]{};
    if (!StringFromGUID2(guid, identifier, 40)) throw std::runtime_error("Cannot format test identifier");
    const auto root = std::filesystem::path(temp) / (std::wstring(L"MdAmpPixelDiff-selftest-") + identifier);
    CheckWin(CreateDirectoryW(root.c_str(), nullptr) != FALSE, "Create self-test directory");
    Image original; original.width = 2; original.height = 2;
    original.pixels = {10,20,30,255, 40,50,60,200, 70,80,90,100, 100,110,120,128};
    SavePng(factory, root / L"reference.png", original);
    SavePng(factory, root / L"equal.png", original);
    Image changed = original; changed.pixels[0] += 1;
    SavePng(factory, root / L"changed.png", changed);
    changed = original; changed.pixels[3] -= 1;
    SavePng(factory, root / L"alpha.png", changed);
    Image narrow; narrow.width = 1; narrow.height = 2; narrow.pixels.resize(8, 255);
    SavePng(factory, root / L"dimensions.png", narrow);
    int checks = 0;
    const auto expect = [&checks](bool good, const char* message) {
        if (!good) throw std::runtime_error(std::string("Self-test failed: ") + message);
        ++checks;
    };
    const auto run = [&factory, &root](const wchar_t* candidate, const wchar_t* output) {
        try { return Compare(factory, root / L"reference.png", root / candidate, root / output); }
        catch (const std::exception&) { return 2; }
    };
    expect(run(L"equal.png", L"equal-result") == 0, "equal BGRA pair");
    expect(run(L"changed.png", L"rgb-result") == 1, "single RGB-channel delta");
    expect(run(L"alpha.png", L"alpha-result") == 1, "alpha-only delta");
    expect(run(L"dimensions.png", L"dimensions-result") == 1, "dimension mismatch");
    expect(run(L"missing.png", L"missing-result") == 2, "missing input");
    expect(!std::filesystem::exists(root / L"missing-result"), "missing input creates no output");
    expect(run(L"equal.png", L"equal-result") == 2, "existing output rejected");
    const BYTE invalid[]{1,2,3,4}; WriteNew(root / L"invalid.png", invalid, sizeof(invalid));
    expect(run(L"invalid.png", L"invalid-result") == 2, "non-PNG rejected");
    Bytes oversized = ReadPng(root / L"reference.png");
    oversized[16] = 127; oversized[17] = 255; oversized[18] = 255; oversized[19] = 255;
    WriteNew(root / L"oversized.png", oversized.data(), oversized.size());
    expect(run(L"oversized.png", L"oversized-result") == 2, "oversized header rejected before pixel allocation");
    const auto heat = Decode(factory, root / L"rgb-result" / L"heatmap.png");
    expect(heat.pixels[0] == 0 && heat.pixels[1] == 0 && heat.pixels[2] == 255 &&
        heat.pixels[3] == 255 && heat.pixels[6] == 0, "red changed / black unchanged heatmap");
    expect(Decode(factory, root / L"reference.png").sha256 == Decode(factory, root / L"equal.png").sha256,
        "SHA-256 retained");
    expect(Hash(Bytes{'a', 'b', 'c'}) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
        "SHA-256 known answer");
    const auto readReport = [&root](const wchar_t* directory) {
        std::ifstream input(root / directory / L"report.json", std::ios::binary);
        if (!input) throw std::runtime_error("Cannot read self-test report");
        std::ostringstream text; text << input.rdbuf(); return text.str();
    };
    const auto rgbReport = readReport(L"rgb-result");
    expect(rgbReport.find("\"changed_pixel_count\": 1") != std::string::npos &&
        rgbReport.find("\"max_channel_delta\": 1") != std::string::npos &&
        rgbReport.find("\"mean_absolute_channel_delta\": 0.0625") != std::string::npos,
        "single-channel exact difference metrics");
    const auto equalReport = readReport(L"equal-result");
    expect(equalReport.find("\"pair_pixel_equal\": true") != std::string::npos &&
        equalReport.find("\"changed_pixel_count\": 0") != std::string::npos &&
        equalReport.find("\"screenshot_provenance_verified\": false") != std::string::npos &&
        equalReport.find("\"full_application_equivalence_verified\": false") != std::string::npos,
        "equal pair never claims screenshot or application proof");
    const auto dimensionsReport = readReport(L"dimensions-result");
    expect(dimensionsReport.find("\"changed_pixel_count\": null") != std::string::npos &&
        dimensionsReport.find("\"pair_pixel_equal\": false") != std::string::npos &&
        dimensionsReport.find("\"heatmap\": null") != std::string::npos &&
        !std::filesystem::exists(root / L"dimensions-result" / L"heatmap.png"),
        "dimension mismatch is not resized or given invented metrics");
    std::cout << checks << " self-tests passed; synthetic fixtures only, NOT Winamp pixel proof.\n";
    std::wcout << L"Self-test artifacts retained at: " << root.c_str() << L'\n';
    return 0;
}
}

int wmain(int argc, wchar_t** argv) {
    const HRESULT initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(initialized)) { std::cerr << "COM initialization failed\n"; return 2; }
    int result = 2;
    try {
        ComPtr<IWICImagingFactory> factory;
        Check(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(&factory)), "Create WIC factory");
        if (argc == 2 && std::wstring(argv[1]) == L"--self-test") result = SelfTest(factory.Get());
        else if (argc == 4) result = Compare(factory.Get(), argv[1], argv[2], argv[3]);
        else std::cerr << "Usage: MdAmpPixelDiff.exe reference.png candidate.png NEW-output-directory\n"
            << "       MdAmpPixelDiff.exe --self-test\n";
    } catch (const std::exception& error) { std::cerr << "Error: " << error.what() << '\n'; }
    CoUninitialize();
    return result;
}
