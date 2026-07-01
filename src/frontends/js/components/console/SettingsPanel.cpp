#include "components/console/SettingsPanel.h"

#include <imgui.h>
#include <magic_enum/magic_enum.hpp>

#include <chrono>
#include <optional>
#include <string>
#include <string_view>

#include "components/profile/ProfileService.h"
#include "components/script/ScriptEngine.h"
#include "config/AppConfig.h"
#include "config/ProfileData.h"
#include "game/GameHelpers.h"
#include "game/Menu.h"
#include "game/Types.h"
#include "speedhack/Speedhack.h"

namespace d2bs::js::console {

namespace {

constexpr ImGuiTableFlags TABLE_FLAGS = ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoBordersInBody;

// Two-column row: left = label, right = control. We push the label as an ID
// so multiple checkboxes with the same hidden `##` label don't collide.
void BeginRow(const char* label) {
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::TextUnformatted(label);
    ImGui::TableNextColumn();
    ImGui::PushID(label);
    ImGui::SetNextItemWidth(-FLT_MIN);
}

void EndRow() {
    ImGui::PopID();
}

// Atomic-backed checkbox. Loads current value, renders, stores on change.
void AtomicCheckbox(const char* label, std::atomic<bool>& field) {
    BeginRow(label);
    bool value = field.load(std::memory_order_relaxed);
    if (ImGui::Checkbox("##v", &value)) {
        field.store(value, std::memory_order_relaxed);
    }
    EndRow();
}

void AtomicInt32(const char* label, std::atomic<int32_t>& field, int32_t minVal, int32_t maxVal) {
    BeginRow(label);
    int32_t value = field.load(std::memory_order_relaxed);
    if (ImGui::DragInt("##v", &value, 1.0F, minVal, maxVal)) {
        field.store(value, std::memory_order_relaxed);
    }
    EndRow();
}

// Speed slider - logarithmic so the useful range (0.01x .. 1000x) is dense in
// the middle. Writes go through speedhack::SetSpeed which handles the clamp
// and re-anchor before storing to AppConfig.speed.
void SpeedSlider(float current) {
    BeginRow("Speed");
    auto value = current;
    // Override BeginRow's full-width default so the Reset button fits on the
    // same line. ~60px is enough for "Reset" in the default font with padding.
    ImGui::SetNextItemWidth(-60.0F);
    if (ImGui::SliderFloat("##v", &value, speedhack::MIN_SPEED, speedhack::MAX_SPEED, "%.2fx",
                           ImGuiSliderFlags_Logarithmic)) {
        speedhack::SetSpeed(value);
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Reset")) {
        speedhack::SetSpeed(1.0F);
    }
    EndRow();
}

// Display-only row for fields set once at init (paths, memory limit, etc.).
// NOLINTNEXTLINE(cert-dcl50-cpp) - matches ImGui's printf-style API
void DisplayRow(const char* label, const char* fmt, ...) {
    BeginRow(label);
    // NOLINTNEXTLINE(cppcoreguidelines-init-variables) - initialized by va_start below
    va_list args;
    // NOLINTBEGIN(cppcoreguidelines-pro-type-vararg, cppcoreguidelines-pro-bounds-array-to-pointer-decay)
    va_start(args, fmt);
    ImGui::TextDisabledV(fmt, args);
    va_end(args);
    // NOLINTEND(cppcoreguidelines-pro-type-vararg, cppcoreguidelines-pro-bounds-array-to-pointer-decay)
    EndRow();
}

// Display-only row naming an enum value via magic_enum (the C++ identifier).
// Falls back to the numeric value if the value is outside the enum's range.
template <typename E>
void EnumRow(const char* label, E value) {
    const auto name = magic_enum::enum_name(value);
    if (name.empty()) {
        DisplayRow(label, "%u", static_cast<uint32_t>(value));
    } else {
        DisplayRow(label, "%.*s", static_cast<int>(name.size()), name.data());
    }
}

}  // namespace

void SettingsPanel::Draw() {
    auto& config = config::GetAppConfig();

    // Scroll this panel's body in its own child so the console tab bar stays
    // pinned as the collapsing sections expand. Other panels are short enough not
    // to need it (or scroll their own child, like Console/Log). WindowPadding is
    // zeroed so the content lines up exactly as when drawn straight into the tab.
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0F, 0.0F));
    ImGui::BeginChild("##settings_body", ImVec2(0.0F, 0.0F));
    ImGui::PopStyleVar();

    ImGui::TextDisabled("Live editor for AppConfig fields. Changes apply immediately.");
    ImGui::Spacing();

    const auto profileName = config.GetProfileName();
    // Reload the active profile only when the name changes - LoadActive reads the INI.
    if (profileName != cachedProfileName_) {
        cachedProfileName_ = profileName;
        cachedProfile_ = profile::LoadActive();
    }

    if (ImGui::CollapsingHeader("Profile")) {
        if (ImGui::BeginTable("##profile", 2, TABLE_FLAGS)) {
            ImGui::TableSetupColumn("##label", ImGuiTableColumnFlags_WidthFixed, 200.0F);
            ImGui::TableSetupColumn("##value", ImGuiTableColumnFlags_WidthStretch);
            DisplayRow("Name", "%s", profileName.empty() ? "(none)" : profileName.c_str());
            if (cachedProfile_.has_value()) {
                const auto& p = *cachedProfile_;
                EnumRow("Type", p.type);
                DisplayRow("Character", "%s", p.character.empty() ? "(none)" : p.character.c_str());
                EnumRow("Difficulty", p.difficulty);
                if (!p.gateway.empty()) {
                    DisplayRow("Gateway", "%s", p.gateway.c_str());
                }
                if (!p.username.empty()) {
                    DisplayRow("Account", "%s", p.username.c_str());
                }
                if (!p.ip.empty()) {
                    DisplayRow("IP", "%s", p.ip.c_str());
                }
                DisplayRow("Max login time", "%lld ms", static_cast<long long>(p.maxLoginTime.count()));
                DisplayRow("Max char-select time", "%lld ms", static_cast<long long>(p.maxCharTime.count()));
            } else if (!profileName.empty()) {
                DisplayRow("Status", "%s", "not found in INI");
            }
            ImGui::EndTable();
        }
    }

    if (ImGui::CollapsingHeader("Runtime")) {
        if (ImGui::BeginTable("##runtime", 2, TABLE_FLAGS)) {
            ImGui::TableSetupColumn("##label", ImGuiTableColumnFlags_WidthFixed, 200.0F);
            ImGui::TableSetupColumn("##value", ImGuiTableColumnFlags_WidthStretch);
            const auto gameState = game::GetGameState();
            EnumRow("Game state", gameState);
            if (gameState == game::GameState::Menu) {
                const auto oog = game::GetOutOfGameLocation();
                const auto oogName = magic_enum::enum_name(oog);
                DisplayRow("OOG location", "%u (%.*s)", static_cast<uint32_t>(oog), static_cast<int>(oogName.size()),
                           oogName.data());
            }
            EnumRow("Difficulty", game::GetDifficulty());
            DisplayRow("Mode", "%s", game::GetGameType() == 0 ? "Classic" : "Expansion");
            DisplayRow("Ping", "%u ms", game::GetPing());
            DisplayRow("FPS", "%u", game::GetFPS());
            const auto gameName = game::GetGameName();
            DisplayRow("Game name", "%s", gameName.empty() ? "(n/a)" : gameName.c_str());
            const auto realm = game::GetRealmName();
            DisplayRow("Realm", "%s", realm.empty() ? "(n/a)" : realm.c_str());
            const auto account = game::GetAccountName();
            DisplayRow("Account", "%s", account.empty() ? "(n/a)" : account.c_str());
            const auto character = game::GetPlayerName();
            DisplayRow("Character", "%s", character.empty() ? "(n/a)" : character.c_str());
            const auto server = game::GetGameServerIp();
            DisplayRow("Game server", "%s", server.empty() ? "(n/a)" : server.c_str());
            ImGui::EndTable();
        }
    }

    if (ImGui::CollapsingHeader("Debugging (V8 inspector)")) {
        if (ImGui::BeginTable("##inspector", 2, TABLE_FLAGS)) {
            ImGui::TableSetupColumn("##label", ImGuiTableColumnFlags_WidthFixed, 200.0F);
            ImGui::TableSetupColumn("##value", ImGuiTableColumnFlags_WidthStretch);
            // inspectorPort's sign encodes enabled/disabled and its magnitude is
            // the listening port. Both controls route through ScriptEngine so the
            // server (re)binds or stops to match - we never write the config here.
            const int32_t stored = config.inspectorPort.load(std::memory_order_relaxed);
            const bool enabled = stored > 0;
            int32_t port = std::abs(stored);
            if (port == 0) {
                port = config::DEFAULT_INSPECTOR_PORT;
            }

            BeginRow("Chrome DevTools");
            bool inspectorOn = enabled;
            if (ImGui::Checkbox("##v", &inspectorOn)) {
                ScriptEngine::Instance().SetInspector(inspectorOn, port);
            }
            EndRow();

            BeginRow("Port");
            if (ImGui::DragInt("##v", &port, 1.0F, config::MIN_INSPECTOR_PORT, config::MAX_INSPECTOR_PORT)) {
                ScriptEngine::Instance().SetInspector(enabled, port);
            }
            EndRow();

            if (enabled) {
                DisplayRow("Connect", "chrome://inspect -> Discover network targets -> Configure -> 127.0.0.1:%d",
                           port);
            }
            ImGui::EndTable();
        }
    }

    if (ImGui::CollapsingHeader("Time")) {
        if (ImGui::BeginTable("##time", 2, TABLE_FLAGS)) {
            ImGui::TableSetupColumn("##label", ImGuiTableColumnFlags_WidthFixed, 200.0F);
            ImGui::TableSetupColumn("##value", ImGuiTableColumnFlags_WidthStretch);
            SpeedSlider(config.speed.load(std::memory_order_relaxed));
            ImGui::EndTable();
        }
    }

    if (ImGui::CollapsingHeader("Chicken / lifecycle")) {
        if (ImGui::BeginTable("##chicken", 2, TABLE_FLAGS)) {
            ImGui::TableSetupColumn("##label", ImGuiTableColumnFlags_WidthFixed, 200.0F);
            ImGui::TableSetupColumn("##value", ImGuiTableColumnFlags_WidthStretch);
            // -1 is kolbot's "disabled" sentinel; allow it as the lower bound.
            AtomicInt32("Chicken HP", config.chickenHp, -1, 100);
            AtomicInt32("Chicken MP", config.chickenMp, -1, 100);
            AtomicCheckbox("Quit on hostile", config.quitOnHostile);
            AtomicCheckbox("Quit on error", config.quitOnError);
            const auto maxGameMs = config.maxGameTime.load(std::memory_order_relaxed).count();
            DisplayRow("Max game time", "%lld ms", static_cast<long long>(maxGameMs));
            ImGui::EndTable();
        }
    }

    if (ImGui::CollapsingHeader("Input")) {
        if (ImGui::BeginTable("##input", 2, TABLE_FLAGS)) {
            ImGui::TableSetupColumn("##label", ImGuiTableColumnFlags_WidthFixed, 200.0F);
            ImGui::TableSetupColumn("##value", ImGuiTableColumnFlags_WidthStretch);
            AtomicCheckbox("Block keys", config.blockKeys);
            AtomicCheckbox("Block mouse", config.blockMouse);
            ImGui::EndTable();
        }
    }

    if (ImGui::CollapsingHeader("Startup")) {
        if (ImGui::BeginTable("##startup", 2, TABLE_FLAGS)) {
            ImGui::TableSetupColumn("##label", ImGuiTableColumnFlags_WidthFixed, 200.0F);
            ImGui::TableSetupColumn("##value", ImGuiTableColumnFlags_WidthStretch);
            AtomicCheckbox("Start at menu", config.startAtMenu);
            AtomicCheckbox("Wait for profile", config.waitForProfile);
            AtomicCheckbox("Enable unsupported", config.enableUnsupported);
            ImGui::EndTable();
        }
    }

    if (ImGui::CollapsingHeader("Read-only (set at init)")) {
        if (ImGui::BeginTable("##ro", 2, TABLE_FLAGS)) {
            ImGui::TableSetupColumn("##label", ImGuiTableColumnFlags_WidthFixed, 200.0F);
            ImGui::TableSetupColumn("##value", ImGuiTableColumnFlags_WidthStretch);
            DisplayRow("Game-ready timeout", "%lld ms", static_cast<long long>(config.gameReadyTimeout.count()));
            DisplayRow("Memory limit", "%zu MB", config.memoryLimit / (1024UL * 1024UL));
            DisplayRow("Idle sleep interval", "%lld ms", static_cast<long long>(config.idleSleepInterval.count()));
            DisplayRow("V8 flags", "%s", config.v8Flags.empty() ? "(none)" : config.v8Flags.c_str());
            if (config.v8SingleThreadedPlatform) {
                DisplayRow("V8 platform", "%s", "single-threaded (no pool)");
            } else if (config.v8ThreadPoolSize == 0) {
                DisplayRow("V8 platform", "%s", "default, auto thread pool");
            } else {
                DisplayRow("V8 platform", "default, %d thread(s)", config.v8ThreadPoolSize);
            }
            const auto paths = config.GetScriptPaths();
            DisplayRow("Script base", "%s", paths.basePath.string().c_str());
            DisplayRow("Game script", "%s", paths.gameScript.c_str());
            DisplayRow("Starter script", "%s", paths.starterScript.c_str());
            DisplayRow("Console script", "%s", paths.consoleScript.c_str());
            ImGui::EndTable();
        }
    }

    ImGui::EndChild();
}

}  // namespace d2bs::js::console
