#pragma once

#include <optional>
#include <string>

#include "components/console/Panel.h"
#include "components/profile/ProfileData.h"

namespace d2bs::framework::console {

// Live editor for AppConfig fields. Each control reads the current atomic
// value on every frame (no caching) and writes through on change - atomic
// loads/stores from the render thread are safe alongside the script-thread
// readers/writers. Speed is special: writes flow through speedhack::SetSpeed
// so the time-domain bases re-anchor before the published value changes.
class SettingsPanel : public Panel {
   public:
    [[nodiscard]] const char* Title() const override { return "Settings"; }
    void Draw() override;

   private:
    // Active profile, reloaded only when the profile name changes (LoadActive
    // reads the INI, so this avoids a disk read every frame).
    std::string cachedProfileName_;
    std::optional<config::ProfileData> cachedProfile_;
};

}  // namespace d2bs::framework::console
