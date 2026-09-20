#pragma once

namespace d2bs::js::console {

// Abstract base for each tab in the console. Panels are owned by the
// console module (in Console.cpp) and drawn once per frame while their
// tab is active. Panels with cross-thread input (e.g. LogPanel receiving
// messages from any thread) expose their own thread-safe entry points;
// only the host's render thread calls Draw().
class Panel {
   public:
    Panel() = default;
    virtual ~Panel() = default;

    Panel(const Panel&) = delete;
    Panel& operator=(const Panel&) = delete;
    Panel(Panel&&) = delete;
    Panel& operator=(Panel&&) = delete;

    // `const char*` (not std::string_view) - dictated by ImGui::BeginTabItem
    // which requires a null-terminated string. The pointer must remain valid
    // for the duration of the call (string literals satisfy this trivially).
    [[nodiscard]] virtual const char* Title() const = 0;
    virtual void Draw() = 0;
};

}  // namespace d2bs::js::console
