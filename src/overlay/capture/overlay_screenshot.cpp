#include "overlay/overlay_internal.h"
#include "overlay/capture/screenshot_service.h"
#include "steam/steam_utils.h"

void StarOverlay::request_screenshot()
{
    if (!enabled_) return;
    screenshots_.request();
    STAR_LOG("Screenshot requested");
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
             : "Saved to " + ScreenshotService::dir(),
        icon_rgba, iw, ih, "SCREENSHOT SAVED");
    // Pop the viewer so the user gets an instant preview on next panel open.
    viewer_file_ = file;
    viewer_pending_ = true;
    STAR_LOG("Screenshot saved: %s", file.c_str());
}
