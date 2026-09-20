#include "components/drawing/VersionBanner.h"

#include <cstdint>
#include <optional>
#include <string>

#include <fmt/format.h>

#include "components/update/UpdateChecker.h"
#include "config/Version.h"
#include "game/GameHelpers.h"
#include "game/Types.h"

namespace d2bs::js::drawing {

void DrawVersionBanner() {
    constexpr uint32_t BANNER_FONT = 0;
    constexpr uint32_t BANNER_COLOR = 4;  // gold
    constexpr uint32_t NOTICE_COLOR = 8;  // orange

    const std::string bannerText = "d2bsng " D2BS_VERSION;
    const auto screen = game::GetViewportSize();
    const auto bannerWidth = static_cast<int32_t>(game::GetTextSize(bannerText, BANNER_FONT).width);

    // D2 draws text up from the baseline, so the last row keeps glyphs on-screen.
    const int32_t baselineY = static_cast<int32_t>(screen.height) - 1;
    const int32_t rightEdge = static_cast<int32_t>(screen.width) - 1;

    const auto available = update::UpdateChecker::Instance().AvailableUpdate();
    if (available) {
        const std::string noticeText =
            fmt::format("({}.{}.{} available)", available->major, available->minor, available->patch);
        const auto noticeWidth = static_cast<int32_t>(game::GetTextSize(noticeText, BANNER_FONT).width);
        const auto spaceWidth = static_cast<int32_t>(game::GetTextSize(" ", BANNER_FONT).width);

        const game::Point noticePos{.x = rightEdge - noticeWidth, .y = baselineY};
        const game::Point bannerPos{.x = noticePos.x - spaceWidth - bannerWidth, .y = baselineY};
        game::DrawGameText(bannerText, bannerPos, BANNER_COLOR, BANNER_FONT);
        game::DrawGameText(noticeText, noticePos, NOTICE_COLOR, BANNER_FONT);
        return;
    }

    const game::Point bannerPos{.x = rightEdge - bannerWidth, .y = baselineY};
    game::DrawGameText(bannerText, bannerPos, BANNER_COLOR, BANNER_FONT);
}

}  // namespace d2bs::js::drawing
