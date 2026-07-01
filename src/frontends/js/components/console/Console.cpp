#include "components/console/Console.h"

#include <imgui.h>

#include <deque>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

#include "components/console/ConsolePanel.h"
#include "components/console/LogPanel.h"
#include "components/console/Panel.h"
#include "components/console/ScriptPanel.h"
#include "components/console/SettingsPanel.h"
#include "components/console/StacktracesPanel.h"
#include "components/console/ThreadsPanel.h"
#include "components/script/Script.h"
#include "components/script/ScriptEngine.h"

namespace d2bs::js::console {

namespace {

constexpr size_t MAX_PENDING = 5000;

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
    state.panels.push_back(std::make_unique<SettingsPanel>());
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
        for (const auto& script : ScriptEngine::Instance().GetAllScripts()) {
            script->SetStackCaptureMode(StackCaptureMode::Off);
        }
    }
    state.visible = visible;
    if (!visible) {
        return;  // queue drained above; nothing to render while hidden
    }

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    constexpr ImGuiWindowFlags WND_FLAGS = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                           ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                                           ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoSavedSettings;
    ImGui::Begin("##console_root", nullptr, WND_FLAGS);

    if (ImGui::BeginTabBar("##tabs")) {
        for (const auto& panel : state.panels) {
            if (ImGui::BeginTabItem(panel->Title())) {
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
    while (pending.size() > MAX_PENDING) {
        pending.pop_front();
    }
}

}  // namespace d2bs::js::console
