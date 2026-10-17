// Offline synthetic rendering of MdAmp's own production GDI painters.
// This is not a desktop/window capture and is not a Winamp comparison fixture.
#include "modern_ui.h"
#include "playlist_ui.h"
#include <wincodec.h>
#include <wrl/client.h>
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
using Microsoft::WRL::ComPtr;
constexpr UINT width = 510;
constexpr UINT playerWidth = 487;
constexpr UINT playerHeight = 280;
constexpr UINT playlistHeight = 976;
constexpr UINT height = 1276;

void Check(HRESULT result, const char* operation) {
    if (FAILED(result)) throw std::runtime_error(operation);
}

struct Surface {
    HDC dc = nullptr;
    HBITMAP bitmap = nullptr;
    HGDIOBJ oldBitmap = nullptr;
    BYTE* pixels = nullptr;

    Surface() {
        dc = CreateCompatibleDC(nullptr);
        if (!dc) throw std::runtime_error("Create preview memory DC failed");
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = static_cast<LONG>(width);
        info.bmiHeader.biHeight = -static_cast<LONG>(height);
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        void* data = nullptr;
        bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &data, nullptr, 0);
        if (!bitmap || !data) {
            if (bitmap) DeleteObject(bitmap);
            DeleteDC(dc);
            throw std::runtime_error("Create preview DIB failed");
        }
        pixels = static_cast<BYTE*>(data);
        oldBitmap = SelectObject(dc, bitmap);
        std::fill_n(pixels, static_cast<std::size_t>(width) * height * 4, BYTE{0});
    }
    ~Surface() {
        SelectObject(dc, oldBitmap);
        DeleteObject(bitmap);
        DeleteDC(dc);
    }
    Surface(const Surface&) = delete;
    Surface& operator=(const Surface&) = delete;
};

void SampleText(HDC dc, RECT rect, const wchar_t* label, int fontHeight, UINT align) {
    const HFONT font = CreateFontW(-fontHeight, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Tahoma");
    const HGDIOBJ oldFont = SelectObject(dc, font);
    const int oldMode = SetBkMode(dc, TRANSPARENT);
    const COLORREF oldColor = SetTextColor(dc, mdamp::PlaylistText());
    DrawTextW(dc, label, -1, &rect, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | align);
    SetTextColor(dc, oldColor);
    SetBkMode(dc, oldMode);
    SelectObject(dc, oldFont);
    DeleteObject(font);
}

void Render(Surface& surface) {
    mdamp::PlayerView view;
    view.width = static_cast<int>(playerWidth);
    view.drawer = true;
    view.title = L"bimbim (0:02)";
    view.time = L"00:00";
    view.info = L"0:02";
    view.volume = .15f;
    view.eqEnabled = true;
    view.eq = {8, 6, -1, -3, -2, 0, 4, 2, 5, 3};
    const int canvasSaved = SaveDC(surface.dc);
    // Observed scene placement, not automatic alignment or a reference image copy.
    // Unrendered desktop pixels stay black and remain part of strict comparison.
    SetViewportOrgEx(surface.dc, 8, 5, nullptr);
    mdamp::PaintPlayer(surface.dc, view);
    const int saved = SaveDC(surface.dc);
    SetViewportOrgEx(surface.dc, 8, 5 + static_cast<int>(playerHeight + 1), nullptr);
    mdamp::PaintPlaylistChrome(surface.dc, static_cast<int>(playerWidth),
        static_cast<int>(playlistHeight), 0, L"0:00/0:02");
    const auto layout = mdamp::ComputePlaylistLayout(static_cast<int>(playerWidth),
        static_cast<int>(playlistHeight));
    SampleText(surface.dc, layout.query, L"Search in Playlist",
        mdamp::kPlaylistQueryFontHeight, DT_LEFT);
    const RECT row{layout.list.left + 1, layout.list.top + 1,
        layout.list.right - 16, layout.list.top + 27};
    RECT title = row;
    title.right -= 59;
    SampleText(surface.dc, title, L"1. bimbim", mdamp::kPlaylistListFontHeight, DT_LEFT);
    RECT duration = row;
    duration.left = duration.right - 59;
    SampleText(surface.dc, duration, L"0:02", mdamp::kPlaylistListFontHeight, DT_RIGHT);
    RestoreDC(surface.dc, saved);
    RestoreDC(surface.dc, canvasSaved);
    GdiFlush();
    // GDI's 32-bit BI_RGB alpha bytes are not meaningful. Export an opaque PNG.
    for (std::size_t i = 3; i < static_cast<std::size_t>(width) * height * 4; i += 4)
        surface.pixels[i] = 255;
}

void SaveNewPng(const std::filesystem::path& path, const Surface& surface) {
    ComPtr<IWICImagingFactory> factory;
    Check(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&factory)), "Create WIC imaging factory failed");
    ComPtr<IStream> stream;
    Check(CreateStreamOnHGlobal(nullptr, TRUE, &stream), "Create encoding stream failed");
    ComPtr<IWICBitmapEncoder> encoder;
    Check(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder),
        "Create PNG encoder failed");
    Check(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache), "Initialize PNG encoder failed");
    ComPtr<IWICBitmapFrameEncode> frame;
    ComPtr<IPropertyBag2> properties;
    Check(encoder->CreateNewFrame(&frame, &properties), "Create PNG frame failed");
    Check(frame->Initialize(properties.Get()), "Initialize PNG frame failed");
    Check(frame->SetSize(width, height), "Set PNG dimensions failed");
    WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
    Check(frame->SetPixelFormat(&format), "Set PNG format failed");
    if (!IsEqualGUID(format, GUID_WICPixelFormat32bppBGRA))
        throw std::runtime_error("PNG encoder changed requested pixel format");
    Check(frame->WritePixels(height, width * 4, width * height * 4, surface.pixels),
        "Encode PNG pixels failed");
    Check(frame->Commit(), "Commit PNG frame failed");
    Check(encoder->Commit(), "Commit PNG encoder failed");
    STATSTG stat{};
    Check(stream->Stat(&stat, STATFLAG_NONAME), "Read PNG stream size failed");
    if (!stat.cbSize.QuadPart || stat.cbSize.QuadPart > 16 * 1024 * 1024)
        throw std::runtime_error("Encoded preview size is invalid");
    std::vector<BYTE> bytes(static_cast<std::size_t>(stat.cbSize.QuadPart));
    LARGE_INTEGER start{};
    Check(stream->Seek(start, STREAM_SEEK_SET, nullptr), "Rewind PNG stream failed");
    ULONG read = 0;
    Check(stream->Read(bytes.data(), static_cast<ULONG>(bytes.size()), &read),
        "Read encoded PNG failed");
    if (read != bytes.size()) throw std::runtime_error("Incomplete encoded PNG read");
    const HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
        CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        throw std::runtime_error("Create new output PNG failed; existing files are never overwritten");
    DWORD written = 0;
    const BOOL writeResult = WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr);
    const BOOL closeResult = CloseHandle(file);
    if (!writeResult || !closeResult || written != bytes.size())
        throw std::runtime_error("Write output PNG failed (new file may be incomplete)");
}
}

int wmain(int argc, wchar_t** argv) {
    if (argc != 2) {
        std::cerr << "Usage: MdAmpRenderPreview NEW-output.png\n"
            "Renders a synthetic fixture through MdAmp's own production GDI painters.\n"
            "Not a captured application window and not pixel-parity evidence.\n";
        return 2;
    }
    const HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(initialized)) {
        std::cerr << "COM initialization failed\n";
        return 2;
    }
    int result = 0;
    try {
        Surface surface;
        Render(surface);
        SaveNewPng(std::filesystem::path(argv[1]), surface);
        std::wcout << L"Wrote own-renderer synthetic preview: " << argv[1] << L" (510 x 1276; player 487 x 280; playlist 487 x 976)\n"
            L"Synthetic media and search text, not native child HWNDs or a desktop capture.\n"
            L"Winamp pixel equivalence and runtime screenshot provenance: NOT VERIFIED.\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        result = 2;
    }
    CoUninitialize();
    return result;
}
