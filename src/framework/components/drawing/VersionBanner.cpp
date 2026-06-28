#include "components/drawing/VersionBanner.h"

#include <cstdint>
#include <string>

#include "components/config/Version.h"
#include "components/update/UpdateChecker.h"
#include "game/GameHelpers.h"
#include "game/Types.h"

namespace d2bs::framework::drawing {

void DrawVersionBanner() {
    // Font/size and palette indices mirror reference ScreenHook.cpp:DrawLogo.
    // game/Console.h documents the in-game color palette (4 = gold, 8 = orange).
    constexpr uint32_t BANNER_FONT = 0;
    constexpr uint32_t BANNER_COLOR = 4;  // gold
    constexpr uint32_t MARKER_COLOR = 8;  // orange - "update available" attention color

    const std::string bannerText = "d2bsng " D2BS_VERSION;
    const auto screen = game::GetViewportSize();
    const auto textWidth = static_cast<int32_t>(game::GetTextSize(bannerText, BANNER_FONT).width);

    // Bottom-right corner, 1px inset. D2 draws text up from its baseline, so a
    // y on the last screen row keeps the glyphs fully on-screen (reference
    // DrawLogo uses the same screenHeight - 1).
    const game::Point bannerPos{.x = static_cast<int32_t>(screen.width) - textWidth - 1,
                                .y = static_cast<int32_t>(screen.height) - 1};
    game::DrawGameText(bannerText, bannerPos, BANNER_COLOR, BANNER_FONT);

    if (update::UpdateChecker::Instance().UpdateAvailable()) {
        // Sit the marker one space-width to the left of the banner: measure
        // "! " (glyph plus the gap) but draw only the glyph.
        const auto markerGap = static_cast<int32_t>(game::GetTextSize("! ", BANNER_FONT).width);
        const game::Point markerPos{.x = bannerPos.x - markerGap, .y = bannerPos.y};
        game::DrawGameText("!", markerPos, MARKER_COLOR, BANNER_FONT);
    }
}

}  // namespace d2bs::framework::drawing
