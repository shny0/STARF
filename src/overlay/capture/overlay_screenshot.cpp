#include "overlay/overlay_internal.h"
#include "core/settings.h"
#include "core/storage.h"
#include "core/callbacks.h"
#include "steam/steam_user_stats.h"
#include "steam/steam_utils.h"
#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx9.h"
#include "imgui_impl_dx11.h"
#include "imgui_impl_dx12.h"
#include "imgui_impl_opengl3.h"
#include "imgui_impl_vulkan.h"
#include <MinHook.h>
#include <d3d9.h>
#include <d3d12.h>
#include <cmath>
#include <wincodec.h>
#pragma comment(lib, "WindowsCodecs.lib")
#include <shlobj.h>
#include <shellapi.h>
#pragma comment(lib, "shell32.lib")
#include <vulkan/vulkan.h>
#include <cctype>
#include <ctime>
#include <algorithm>

void StarOverlay::request_screenshot()
{
    if (!enabled_) return;
    screenshot_requested_ = true;
    STAR_LOG("Screenshot requested");
}

std::string StarOverlay::screenshots_dir()
{
    // Per-game subfolder so shots never mix across titles.
    // Falls back to the game-local STAR folder when Documents is unavailable.
    std::string leaf = "\\STAR\\screenshots\\" + std::to_string(Settings::get().app_id);
    char docs[MAX_PATH] = {};
    if (SUCCEEDED(SHGetFolderPathA(nullptr, CSIDL_PERSONAL, nullptr, SHGFP_TYPE_CURRENT, docs)) && docs[0])
        return std::string(docs) + leaf;
    return Settings::get().settings_dir + leaf;
}

std::string StarOverlay::next_screenshot_path()
{
    std::string dir = screenshots_dir();
    Storage::ensure_dir(dir);
    SYSTEMTIME st{};
    GetLocalTime(&st);
    char base[128];
    snprintf(base, sizeof(base), "STAR_%u_%04d%02d%02d_%02d%02d%02d",
        Settings::get().app_id, (int)st.wYear, (int)st.wMonth, (int)st.wDay,
        (int)st.wHour, (int)st.wMinute, (int)st.wSecond);
    for (int i = 0; i < 100; i++) {
        char full[MAX_PATH];
        if (i == 0) snprintf(full, sizeof(full), "%s\\%s.png", dir.c_str(), base);
        else snprintf(full, sizeof(full), "%s\\%s_%d.png", dir.c_str(), base, i);
        if (GetFileAttributesA(full) == INVALID_FILE_ATTRIBUTES) return full;
    }
    return "";
}

void StarOverlay::notify_screenshot(const std::string& file, bool dark)
{
    std::string name = file;
    size_t slash = name.find_last_of("\\/");
    if (slash != std::string::npos) name = name.substr(slash + 1);
    // Thumbnail preview so the toast isn't a grey box.
    std::vector<uint8_t> icon_rgba;
    int iw = 0, ih = 0;
    StarSteamUtils::get().LoadIconFile(file, icon_rgba, iw, ih);
    push_achievement(name,
        dark ? "All black? Try Borderless mode."
             : "Saved to " + screenshots_dir(),
        icon_rgba, iw, ih, "SCREENSHOT SAVED");
    // Pop the viewer so the user gets an instant preview on next panel open.
    viewer_file_ = file;
    viewer_pending_ = true;
    STAR_LOG("Screenshot saved: %s", file.c_str());
}

bool StarOverlay::save_rgba_png(const std::string& path, const uint8_t* rgba, int w, int h)
{
    if (path.empty() || !rgba || w <= 0 || h <= 0 || w > 16384 || h > 16384) return false;
    // Screenshots must be opaque: backbuffers often carry garbage/zero in
    // alpha (X8 formats, GL default framebuffer), which viewers show as
    // black or checkered "strange colors".
    std::vector<uint8_t> px((size_t)w * h * 4);
    for (size_t i = 0; i < (size_t)w * h; i++) {
        px[i * 4 + 0] = rgba[i * 4 + 0];
        px[i * 4 + 1] = rgba[i * 4 + 1];
        px[i * 4 + 2] = rgba[i * 4 + 2];
        px[i * 4 + 3] = 255;
    }
    const uint8_t* data = px.data();
    bool com_here = false;
    HRESULT cohr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    if (cohr == S_OK) com_here = true;
    else if (FAILED(cohr) && cohr != RPC_E_CHANGED_MODE) return false;

    bool ok = false;
    IWICImagingFactory* factory = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
            IID_IWICImagingFactory, (void**)&factory)) && factory) {
        IWICStream* stream = nullptr;
        IWICBitmapEncoder* encoder = nullptr;
        IWICBitmapFrameEncode* frame = nullptr;
        int wlen = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, nullptr, 0);
        std::wstring wpath((size_t)(wlen > 0 ? wlen : 1), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, wpath.data(), wlen);
        if (SUCCEEDED(factory->CreateStream(&stream)) && stream &&
            SUCCEEDED(stream->InitializeFromFilename(wpath.c_str(), GENERIC_WRITE)) &&
            SUCCEEDED(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder)) && encoder &&
            SUCCEEDED(encoder->Initialize(stream, WICBitmapEncoderNoCache)) &&
            SUCCEEDED(encoder->CreateNewFrame(&frame, nullptr)) && frame &&
            SUCCEEDED(frame->Initialize(nullptr)) &&
            SUCCEEDED(frame->SetSize((UINT)w, (UINT)h))) {
            GUID fmt = GUID_WICPixelFormat32bppRGBA;
            // NOTE: the PNG encoder may coerce this (typically to BGRA);
            // the buffer must match whatever comes back, or R/B swap.
            if (SUCCEEDED(frame->SetPixelFormat(&fmt))) {
                const uint8_t* src = data;
                std::vector<uint8_t> bgra;
                bool fmt_ok = true;
                if (IsEqualGUID(fmt, GUID_WICPixelFormat32bppBGRA)) {
                    bgra.resize((size_t)w * h * 4);
                    for (size_t i = 0; i < (size_t)w * h; i++) {
                        bgra[i * 4 + 0] = data[i * 4 + 2];
                        bgra[i * 4 + 1] = data[i * 4 + 1];
                        bgra[i * 4 + 2] = data[i * 4 + 0];
                        bgra[i * 4 + 3] = data[i * 4 + 3];
                    }
                    src = bgra.data();
                } else if (!IsEqualGUID(fmt, GUID_WICPixelFormat32bppRGBA)) {
                    STAR_LOG("Screenshot: unexpected pixel format, aborting %s", path.c_str());
                    fmt_ok = false;
                }
                if (fmt_ok &&
                    SUCCEEDED(frame->WritePixels((UINT)h, (UINT)w * 4, (UINT)(w * h * 4), (BYTE*)src)) &&
                    SUCCEEDED(frame->Commit()) && SUCCEEDED(encoder->Commit())) {
                    ok = true;
                }
            }
        }
        if (frame) frame->Release();
        if (encoder) encoder->Release();
        if (stream) stream->Release();
        factory->Release();
    }
    if (com_here) CoUninitialize();
    if (!ok) STAR_LOG("Screenshot encode failed: %s", path.c_str());
    return ok;
}

// Captures run AFTER the overlay draws, so screenshots show what the user
// sees (panel, toasts, HUD included).

