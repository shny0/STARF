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

void StarOverlay::resolve_accent()
{
    const std::string& a = Settings::get().overlay_accent;
    if (a == "red")         { acc_r_ = 0xff; acc_g_ = 0x5a; acc_b_ = 0x5a; }
    else if (a == "green")  { acc_r_ = 0x4c; acc_g_ = 0xb8; acc_b_ = 0x4c; }
    else if (a == "purple") { acc_r_ = 0xb0; acc_g_ = 0x7f; acc_b_ = 0xff; }
    else if (a == "orange") { acc_r_ = 0xff; acc_g_ = 0xa0; acc_b_ = 0x3c; }
    else if (a == "yellow") { acc_r_ = 0xff; acc_g_ = 0xd4; acc_b_ = 0x4d; }
    else                    { acc_r_ = 0x4f; acc_g_ = 0xa3; acc_b_ = 0xff; } // blue
}

ImU32 StarOverlay::acc(float a) const
{
    return IM_COL32(acc_r_, acc_g_, acc_b_, (int)(a * 255.f + .5f));
}

ImVec4 StarOverlay::vacc(float a) const
{
    return { acc_r_ / 255.f, acc_g_ / 255.f, acc_b_ / 255.f, a };
}

void StarOverlay::setup_imgui_style_and_fonts()
{
    resolve_accent();
    ui_scale_ = Settings::get().overlay_scale;
    if (ui_scale_ < 0.75f) ui_scale_ = 0.75f;
    if (ui_scale_ > 2.0f) ui_scale_ = 2.0f;

    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
    io.FontGlobalScale = ui_scale_;

    char windir[MAX_PATH]{}; GetWindowsDirectoryA(windir, MAX_PATH);
    std::string fd = std::string(windir) + "\\Fonts\\";
    const char* faces[] = { "segoeui.ttf","arial.ttf","tahoma.ttf",nullptr };

    ImFontConfig fc; fc.OversampleH = 4; fc.OversampleV = 4; fc.PixelSnapH = false;

    // Custom TTF first (overlay.star `font`), then system faces.
    std::string custom = Settings::get().overlay_font;
    while (!custom.empty() && (custom.back() == ' ' || custom.back() == '\t' ||
                               custom.back() == '\r' || custom.back() == '\n'))
        custom.pop_back();
    std::string custom_path;
    if (!custom.empty()) {
        bool absolute = (custom.size() > 1 && custom[1] == ':') || (!custom.empty() && (custom[0] == '\\' || custom[0] == '/'));
        // Relative names live in STAR/Fonts (e.g. font = poppins.ttf).
        custom_path = absolute ? custom : Settings::get().settings_dir + "\\Fonts\\" + custom;
        DWORD attr = GetFileAttributesA(custom_path.c_str());
        if (attr == INVALID_FILE_ATTRIBUTES || (attr & FILE_ATTRIBUTE_DIRECTORY)) {
            STAR_LOG("Overlay font not found: %s", custom_path.c_str());
            custom_path.clear();
        }
    }

    auto tryFont = [&](float sz) -> ImFont* {
        if (!custom_path.empty()) {
            if (auto* f = io.Fonts->AddFontFromFileTTF(custom_path.c_str(), sz, &fc)) {
                if (sz == 16.f) STAR_LOG("Overlay font: %s", custom_path.c_str());
                return f;
            }
            STAR_LOG("Overlay font failed to load: %s", custom_path.c_str());
            custom_path.clear();
        }
        for (int i = 0; faces[i]; i++) {
            if (auto* f = io.Fonts->AddFontFromFileTTF((fd+faces[i]).c_str(), sz, &fc))
                return f;
        }
        return nullptr;
    };

    font_small_ = tryFont(14.f);
    font_title_ = tryFont(19.f);
    if (ImFont* fb = tryFont(16.f)) io.FontDefault = fb;
    else io.Fonts->AddFontDefault();

    ImGui::StyleColorsDark();
    ImGuiStyle& s = ImGui::GetStyle();
    s.ScaleAllSizes(ui_scale_);
    s.WindowRounding   = 0.f;
    s.ChildRounding    = 4.f;
    s.FrameRounding    = 4.f;
    s.ScrollbarRounding= 4.f;
    s.GrabRounding     = 4.f;
    s.WindowBorderSize = 1.f;
    s.ChildBorderSize  = 0.f;
    s.FrameBorderSize  = 0.f;
    s.WindowPadding    = { 18.f, 16.f };
    s.FramePadding     = { 10.f,  7.f };
    s.ItemSpacing      = { 10.f, 10.f };
    s.ScrollbarSize    = 8.f;

    auto* C = s.Colors;
    C[ImGuiCol_WindowBg]           = v4(P_BG0, 0.97f);
    C[ImGuiCol_ChildBg]            = v4(P_BG1, 1.f);
    C[ImGuiCol_Border]             = v4(P_SEP, 0.6f);
    C[ImGuiCol_FrameBg]            = v4(P_BG2, 1.f);
    C[ImGuiCol_FrameBgHovered]     = v4(0x30,0x30,0x30, 1.f);
    C[ImGuiCol_Button]             = v4(P_BG2, 1.f);
    C[ImGuiCol_ButtonHovered]      = v4(0x32,0x32,0x32, 1.f);
    C[ImGuiCol_ButtonActive]       = v4(0x3a,0x3a,0x3a, 1.f);
    C[ImGuiCol_Header]             = v4(P_BG2, 1.f);
    C[ImGuiCol_HeaderHovered]      = v4(0x30,0x30,0x30, 1.f);
    C[ImGuiCol_ScrollbarBg]        = v4(P_BG0, 1.f);
    C[ImGuiCol_ScrollbarGrab]      = v4(P_SEP, 1.f);
    C[ImGuiCol_ScrollbarGrabHovered]= v4(0x45,0x45,0x45, 1.f);
    C[ImGuiCol_ScrollbarGrabActive] = vacc(1.f);
    C[ImGuiCol_Separator]          = v4(P_SEP, 0.5f);
    C[ImGuiCol_Text]               = v4(P_TXT, 1.f);
    C[ImGuiCol_TextDisabled]       = v4(P_MUT, 1.f);
    C[ImGuiCol_TitleBg]            = v4(P_BG0, 1.f);
    C[ImGuiCol_TitleBgActive]      = v4(P_BG0, 1.f);
    C[ImGuiCol_PopupBg]            = v4(P_BG1, 0.98f);
    C[ImGuiCol_CheckMark]          = vacc(1.f);
    C[ImGuiCol_SliderGrab]         = vacc(1.f);
}

