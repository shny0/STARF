#pragma once
// Declarations shared by the split overlay translation units. The StarOverlay
// class itself still lives in overlay.h; this header only holds the few symbols
// that multiple TUs need to agree on.

#include "overlay/overlay.h"
#include "overlay/core/overlay_util.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

// Set once in core/overlay.cpp when the overlay initializes, cleared on
// shutdown. Every render/input hook reads it and passes through when null.
extern StarOverlay* g_overlay;
