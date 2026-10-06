#include "components/console/Console.h"

#include <imgui.h>

#include <algorithm>
#include <cfloat>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "components/console/ConsolePanel.h"
#include "components/console/LogPanel.h"
#include "components/console/Panel.h"
#include "components/console/ProfilingPanel.h"
#include "components/console/ScriptPanel.h"
#include "components/console/SettingsPanel.h"
#include "components/console/StacktracesPanel.h"
#include "components/console/ThreadsPanel.h"
#include "components/script/Script.h"
#include "components/script/ScriptEngine.h"

namespace d2bs::runtime::console {

namespace {

// Cross-thread inbound queue. OnMessage appends; DrawFrame swaps it out.
std::mutex queueMutex;
// NOLINTNEXTLINE(cert-err58-cpp) - default-constructed deque, no real throw risk
std::deque<game::console::Message> pending;

struct State {
    bool initialized = false;
    bool visible = false;
    LogPanel* logPanel = nullptr;
    ConsolePanel* consolePanel = nullptr;
    std::vector<std::unique_ptr<Panel>> panels;
    std::vector<std::unique_ptr<game::console::BackendPanel>> backendPanels;
};

State& GetState() {
    static State s;
    return s;
}

void Initialize(State& state) {
    if (state.initialized) {
        return;
    }
    auto logOwning = std::make_unique<LogPanel>();
    state.logPanel = logOwning.get();
    state.panels.push_back(std::move(logOwning));

    auto consoleOwning = std::make_unique<ConsolePanel>();
    state.consolePanel = consoleOwning.get();
    state.panels.push_back(std::move(consoleOwning));

    state.panels.push_back(std::make_unique<ScriptPanel>());
    state.panels.push_back(std::make_unique<StacktracesPanel>());
    state.panels.push_back(std::make_unique<ThreadsPanel>());
#ifdef D2BS_PROFILING
    state.panels.push_back(std::make_unique<ProfilingPanel>());
#endif
    state.panels.push_back(std::make_unique<SettingsPanel>());
    state.backendPanels = game::console::GetBackendPanels();
    state.initialized = true;
}

void DrainQueue(State& state) {
    std::deque<game::console::Message> batch;
    {
        const std::scoped_lock guard(queueMutex);
        std::swap(batch, pending);
    }
    for (auto& msg : batch) {
        if (msg.source == game::console::MessageSource::EvaluateResult ||
            msg.source == game::console::MessageSource::ConsolePrint) {
            if (state.consolePanel != nullptr) {
                state.consolePanel->Append(msg);
            }
        } else {
            if (state.logPanel != nullptr) {
                state.logPanel->Append(std::move(msg));
            }
        }
    }
}

// The console is the whole of its own host window.
void BeginHostWindow() {
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    constexpr ImGuiWindowFlags WND_FLAGS = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                           ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                                           ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoSavedSettings;
    ImGui::Begin("##console_root", nullptr, WND_FLAGS);
}

// The console floats over the game, which stays visible and clickable around it.
void BeginOverlayWindow() {
    constexpr float DEFAULT_SIZE_FRACTION = 0.6F;
    constexpr ImVec2 MIN_SIZE{480.0F, 240.0F};

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const ImVec2 center{vp->WorkPos.x + (vp->WorkSize.x * 0.5F), vp->WorkPos.y + (vp->WorkSize.y * 0.5F)};
    ImGui::SetNextWindowPos(center, ImGuiCond_FirstUseEver, ImVec2{0.5F, 0.5F});
    ImGui::SetNextWindowSize(ImVec2{vp->WorkSize.x * DEFAULT_SIZE_FRACTION, vp->WorkSize.y * DEFAULT_SIZE_FRACTION},
                             ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(MIN_SIZE, ImVec2{FLT_MAX, FLT_MAX});
    bool isOpen = true;
    ImGui::Begin("d2bsng###console_root", &isOpen, ImGuiWindowFlags_NoSavedSettings);
    if (!isOpen) {
        game::console::Hide();
    }

    // The window may hang past any edge, but enough of its title bar stays inside
    // the viewport to grab it again, also when the game window shrinks.
    constexpr float GRAB_MARGIN = 64.0F;
    const ImVec2 pos = ImGui::GetWindowPos();
    const ImVec2 size = ImGui::GetWindowSize();
    const float titleBarHeight = ImGui::GetFrameHeight();
    const ImVec2 clamped{
        std::max(vp->WorkPos.x + GRAB_MARGIN - size.x, std::min(pos.x, vp->WorkPos.x + vp->WorkSize.x - GRAB_MARGIN)),
        std::max(vp->WorkPos.y, std::min(pos.y, vp->WorkPos.y + vp->WorkSize.y - titleBarHeight)),
    };
    if (clamped.x != pos.x || clamped.y != pos.y) {
        ImGui::SetWindowPos(clamped);
    }
}

}  // namespace

void DrawFrame() {
    State& state = GetState();
    Initialize(state);
    DrainQueue(state);

    const bool visible = game::console::IsVisible();
    if (state.visible && !visible) {
        // Console just hidden: stop all per-script stack capture so a script left
        // selected in the Stacktraces panel doesn't keep walking its V8 stack at
        // every delay(). The panel re-enables the selected script on show.
        for (const auto& script : script::ScriptEngine::Instance().GetAllScripts()) {
            script->SetStackCaptureMode(script::StackCaptureMode::Off);
        }
    }
    state.visible = visible;
    if (!visible) {
        return;  // queue drained above; nothing to render while hidden
    }

    if (game::console::IsInGameOverlay()) {
        BeginOverlayWindow();
    } else {
        BeginHostWindow();
    }

    if (ImGui::BeginTabBar("##tabs")) {
        for (const auto& panel : state.panels) {
            if (ImGui::BeginTabItem(panel->Title())) {
                panel->Draw();
                ImGui::EndTabItem();
            }
        }
        for (const auto& panel : state.backendPanels) {
            const std::string title = panel->Title();
            if (ImGui::BeginTabItem(title.c_str())) {
                panel->Draw();
                ImGui::EndTabItem();
            }
        }
        ImGui::EndTabBar();
    }

    ImGui::End();
}

void OnMessage(const game::console::Message& msg) {
    const std::scoped_lock guard(queueMutex);
    pending.push_back(msg);
    constexpr size_t MAX_PENDING = 5000;
    while (pending.size() > MAX_PENDING) {
        pending.pop_front();
    }
}

}  // namespace d2bs::runtime::console
