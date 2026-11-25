#include "console/PacketInspector.h"

#include <Windows.h>
#include <imgui.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <format>
#include <fstream>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_set>
#include <utility>
#include <vector>

#include "hooks/Intercepts.h"
#include "packets/Packet.h"
#include "packets/Registry.h"
#include "utils/Strings.h"
#include "utils/threadutils.h"
#include "utils/utils.h"

namespace d2bs::lod114d::console::packet_inspector {

namespace {

spdlog::logger& Log() {
    static const auto LOGGER = utils::GetLogger("packets.inspector");
    return *LOGGER;
}

// One packet as it crossed the intercepts. Capture runs inside the game's own receive and send calls, so it keeps
// the bytes and nothing it would have to work out; what the entry decodes to is worked out the first time something
// asks, and kept. Everything that reads an Entry holds the recorder's mutex, so the caches need no lock of their own.
struct Entry {
    uint32_t batch = 0;
    bool fromServer = false;
    bool fromScript = false;
    uint8_t id = 0;
    std::vector<uint8_t> bytes;

    mutable std::optional<bool> handled;
    mutable std::optional<std::string> description;
};

// The panel's controls, as much of them as the recorder needs: which entries it logs and records, and how many it
// keeps. The panel pushes its copy into the recorder every frame it draws.
struct Settings {
    bool showServer = false;
    bool showClient = false;
    bool showHex = false;
    bool hideHandled = false;
    bool logToConsole = false;
    uint32_t history = DEFAULT_HISTORY;
    std::unordered_set<uint8_t> excluded;
    std::unordered_set<uint8_t> included;
    std::unordered_set<uint8_t> stacktraced;
};

std::unordered_set<uint8_t> ParseIdSet(std::string_view text) {
    std::unordered_set<uint8_t> ids;
    for (const auto& token : utils::Split(text, " ,")) {
        std::string_view view = token;
        if (view.starts_with("0x") || view.starts_with("0X")) {
            view.remove_prefix(2);
        }
        uint32_t value = 0;
        if (const auto result = std::from_chars(view.data(), view.data() + view.size(), value, 16);
            result.ec == std::errc{} && result.ptr == view.data() + view.size() && value <= UINT8_MAX) {
            ids.insert(static_cast<uint8_t>(value));
        }
    }
    return ids;
}

// Null when the id has no class or the bytes do not decode, which is what makes the entry show as bytes.
std::unique_ptr<packets::Packet> Decode(const Entry& entry) {
    auto packet = entry.fromServer ? packets::PacketRegistry::MakeServerPacket(entry.id)
                                   : packets::PacketRegistry::MakeClientPacket(entry.id);
    if (!packet || !packet->FromBytes(entry.bytes)) {
        return nullptr;
    }
    return packet;
}

bool IsHandled(const Entry& entry) {
    if (!entry.handled.has_value()) {
        entry.handled = Decode(entry) != nullptr;
    }
    return *entry.handled;
}

std::string_view DirectionTag(const Entry& entry) {
    return entry.fromServer ? "[S->C]" : "[C->S]";
}

std::string_view ScriptTag(const Entry& entry) {
    return entry.fromScript ? " [script]" : "";
}

std::string HexLine(const Entry& entry) {
    return std::format("{} [{:#04x}, {:3}] {}{}", DirectionTag(entry), entry.id, entry.bytes.size(),
                       packets::ToHexDump(entry.bytes), ScriptTag(entry));
}

// The batch parity in the prefix tells the packets one frame handled from the next without spending a column on a
// counter that only grows.
std::string Render(const Entry& entry) {
    const std::string prefix = std::format("{} [{}]", DirectionTag(entry), entry.batch % 2);
    if (const auto parsed = Decode(entry); parsed != nullptr) {
        entry.handled = true;
        return std::format("{} {}{}", prefix, parsed->ToString(), ScriptTag(entry));
    }
    entry.handled = false;
    return std::format("{} [{:#04x}, {:3}] {}{}", prefix, entry.id, entry.bytes.size(), packets::ToHexDump(entry.bytes),
                       ScriptTag(entry));
}

const std::string& Describe(const Entry& entry) {
    if (!entry.description.has_value()) {
        entry.description = Render(entry);
    }
    return *entry.description;
}

bool IsRelevant(const Settings& settings, const Entry& entry) {
    if (entry.fromServer ? !settings.showServer : !settings.showClient) {
        return false;
    }
    if (settings.excluded.contains(entry.id)) {
        return false;
    }
    if (!settings.included.empty() && !settings.included.contains(entry.id)) {
        return false;
    }
    return !settings.hideHandled || !IsHandled(entry);
}

// Next to d2bs.dll, where the logs and d2bs.ini are: Diablo II itself often sits somewhere a bot cannot write.
std::filesystem::path RecordingRoot() {
    HMODULE module = nullptr;
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(&RecordingRoot), &module) != 0) {
        std::array<wchar_t, MAX_PATH> path{};
        const DWORD length = GetModuleFileNameW(module, path.data(), path.size());
        if (length > 0 && length < path.size()) {
            return std::filesystem::path(path.data()).parent_path();
        }
    }
    return std::filesystem::current_path();
}

// The capture buffer the panel is a view onto. The intercepts write it from the thread handling each packet, inside
// the game's own receive and send calls; the panel reads it from the console's render thread. So it outlives every
// panel and owns all the cross-thread state.
class PacketRecorder {
   public:
    // Scoped read of the capture buffer from the render thread. Holds the recorder's lock for its whole lifetime,
    // because the panel keeps pointers into the deque while it draws.
    class View {
       public:
        explicit View(PacketRecorder& recorder) : recorder_(recorder), guard_(recorder.mutex_) {}

        View(const View&) = delete;
        View& operator=(const View&) = delete;
        View(View&&) = delete;
        View& operator=(View&&) = delete;
        ~View() = default;

        [[nodiscard]] const std::deque<Entry>& Entries() const { return recorder_.entries_; }
        void Clear() { recorder_.entries_.clear(); }

       private:
        PacketRecorder& recorder_;
        std::lock_guard<std::mutex> guard_;
    };

    // Render thread. Publishes the panel's controls and trims the buffer to the history they ask for.
    void Apply(Settings settings) {
        const std::lock_guard guard(mutex_);
        settings_ = std::move(settings);
        Trim();
    }

    // Render thread. False when the directory could not be created, which also leaves recording off.
    bool StartRecording() {
        const auto directory = RecordingRoot() / "packet_recordings" / std::format("{}", GetTickCount64());
        std::error_code error;
        std::filesystem::create_directories(directory / "server", error);
        if (!error) {
            std::filesystem::create_directories(directory / "client", error);
        }
        if (error) {
            Log().error("[inspector] cannot create the recording directory: {}", error.message());
            return false;
        }
        const std::lock_guard guard(mutex_);
        recordingFile_.close();
        recordingDirectory_ = directory;
        Log().info("[inspector] recording packets to {}", directory.string());
        return true;
    }

    // Render thread.
    void StopRecording() {
        const std::lock_guard guard(mutex_);
        recordingDirectory_.clear();
        recordingFile_.close();
    }

    // The intercepts' thread for the packet.
    void OnPacket(std::span<const uint8_t> packet, bool fromServer, bool fromScript) {
        const std::lock_guard guard(mutex_);

        if (settings_.stacktraced.contains(packet[0])) {
            Log().info("[inspector] {} {:#04x}: {}", fromServer ? "S->C" : "C->S", packet[0],
                       utils::threads::GetThreadStacktrace());
        }

        // A batch is the packets of one direction one game frame handled.
        const uint32_t frame = hooks::intercepts::GameFrame();
        if (entries_.empty() || frame != batchFrame_ || fromServer != batchFromServer_) {
            ++batch_;
            batchFrame_ = frame;
            batchFromServer_ = fromServer;
            batchTick_ = GetTickCount64();
        }

        entries_.push_back(Entry{
            .batch = batch_,
            .fromServer = fromServer,
            .fromScript = fromScript,
            .id = packet[0],
            .bytes = std::vector(packet.begin(), packet.end()),
        });
        const Entry& entry = entries_.back();
        if (settings_.logToConsole && IsRelevant(settings_, entry)) {
            Log().info("[inspector] {}", settings_.showHex ? HexLine(entry) : Describe(entry));
        }
        WriteRecording(entry);
        Trim();
    }

   private:
    // Callers hold mutex_. Appends the packet's bytes to its batch's file, so a file holds the packets of one direction
    // one frame handled, back to back as they crossed the wire.
    // The batch's file stays open until the next batch starts, so a batch costs one open rather than one per packet.
    void WriteRecording(const Entry& entry) {
        if (recordingDirectory_.empty() || !IsRelevant(settings_, entry)) {
            return;
        }
        if (!recordingFile_.is_open() || recordingBatch_ != entry.batch) {
            recordingFile_.close();
            recordingFile_.open(recordingDirectory_ / (entry.fromServer ? "server" : "client") /
                                    std::format("{}_{}", batchTick_, entry.batch),
                                std::ios::out | std::ios::binary | std::ios::app);
            recordingBatch_ = entry.batch;
        }
        recordingFile_.write(reinterpret_cast<const char*>(entry.bytes.data()),
                             static_cast<std::streamsize>(entry.bytes.size()));
        recordingFile_.flush();
    }

    // Callers hold mutex_.
    void Trim() {
        while (entries_.size() > settings_.history) {
            entries_.pop_front();
        }
    }

    std::mutex mutex_;
    std::deque<Entry> entries_;
    uint32_t batch_ = 0;
    uint32_t batchFrame_ = 0;
    bool batchFromServer_ = false;
    uint64_t batchTick_ = 0;
    Settings settings_;
    std::filesystem::path recordingDirectory_;
    std::ofstream recordingFile_;
    uint32_t recordingBatch_ = 0;
};

// The one recorder. It outlives every panel: the observer is a process-wide registration the intercepts call at any
// time, while a panel lives only as long as the console keeps its tab.
PacketRecorder& Recorder() {
    static PacketRecorder recorder;
    return recorder;
}

void OnPacket(std::span<const uint8_t> packet, bool fromServer, bool fromScript) {
    Recorder().OnPacket(packet, fromServer, fromScript);
}

}  // namespace

void Install() {
    hooks::intercepts::SetPacketObserver(&OnPacket);
}

void Remove() {
    hooks::intercepts::SetPacketObserver(nullptr);
}

Panel::Panel() {
    // The chatter that drowns everything else out: pings, vitals, movement and the per-frame unit updates.
    constexpr std::string_view DEFAULT_EXCLUDE = "0x6d 0x8f 0x95 0x96 0x18 0x67 0x68 0x16";
    std::ranges::copy(DEFAULT_EXCLUDE, excludeText_.begin());
    excluded_ = ParseIdSet(DEFAULT_EXCLUDE);
}

std::string Panel::Title() const {
    return "Packets";
}

void Panel::DrawToolbar() {
    ImGui::Checkbox("Server##inspectorServer", &showServer_);
    ImGui::SameLine();
    ImGui::Checkbox("Client##inspectorClient", &showClient_);
    ImGui::SameLine();
    ImGui::Checkbox("Hex##inspectorHex", &showHex_);
    ImGui::SameLine();
    ImGui::Checkbox("Hide handled##inspectorHideHandled", &hideHandled_);
    ImGui::SameLine();
    ImGui::Checkbox("Log to console##inspectorLog", &logToConsole_);
    ImGui::SameLine();
    if (ImGui::Checkbox("Record##inspectorRecord", &recording_)) {
        if (recording_) {
            recording_ = Recorder().StartRecording();
        } else {
            Recorder().StopRecording();
        }
    }

    ImGui::SameLine();
    ImGui::SetNextItemWidth(100.0F);
    auto history = static_cast<int32_t>(history_);
    if (ImGui::InputInt("History##inspectorHistory", &history)) {
        history_ = static_cast<uint32_t>(std::max(1, history));
    }
}

void Panel::DrawFilters() {
    const float third = ImGui::GetContentRegionAvail().x / 3.0F;

    ImGui::SetNextItemWidth(third);
    if (ImGui::InputText("##inspectorExclude", excludeText_.data(), excludeText_.size())) {
        excluded_ = ParseIdSet(excludeText_.data());
    }
    ImGui::SetItemTooltip("Packet ids to hide");

    ImGui::SameLine();
    ImGui::SetNextItemWidth(third);
    if (ImGui::InputText("##inspectorInclude", includeText_.data(), includeText_.size())) {
        included_ = ParseIdSet(includeText_.data());
    }
    ImGui::SetItemTooltip("Packet ids to show only");

    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
    if (ImGui::InputText("##inspectorStacktrace", stacktraceText_.data(), stacktraceText_.size())) {
        stacktraced_ = ParseIdSet(stacktraceText_.data());
    }
    ImGui::SetItemTooltip("Packet ids to log a stack trace for");
}

void Panel::Draw() {
    DrawToolbar();

    const Settings settings{
        .showServer = showServer_,
        .showClient = showClient_,
        .showHex = showHex_,
        .hideHandled = hideHandled_,
        .logToConsole = logToConsole_,
        .history = history_,
        .excluded = excluded_,
        .included = included_,
        .stacktraced = stacktraced_,
    };
    Recorder().Apply(settings);

    PacketRecorder::View view{Recorder()};

    std::vector<const Entry*> visible;
    if (showServer_ || showClient_) {
        visible.reserve(view.Entries().size());
        for (const auto& entry : view.Entries()) {
            if (IsRelevant(settings, entry)) {
                visible.push_back(&entry);
            }
        }
    }

    ImGui::SameLine();
    if (ImGui::Button("Copy##inspectorCopy")) {
        std::string text;
        for (const auto* entry : visible) {
            text += showHex_ ? HexLine(*entry) : Describe(*entry);
            text += '\n';
        }
        ImGui::SetClipboardText(text.c_str());
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear##inspectorClear")) {
        view.Clear();
        visible.clear();
    }

    DrawFilters();
    ImGui::Separator();

    constexpr ImGuiWindowFlags SCROLL_FLAGS = ImGuiWindowFlags_HorizontalScrollbar |
                                              ImGuiWindowFlags_AlwaysVerticalScrollbar |
                                              ImGuiWindowFlags_AlwaysHorizontalScrollbar;
    if (!ImGui::BeginChild("##inspectorScroll", ImVec2(0.0F, 0.0F), false, SCROLL_FLAGS)) {
        ImGui::EndChild();
        return;
    }

    const float scrollMax = ImGui::GetScrollMaxY();
    const bool atBottom = scrollMax <= 1.0F || ImGui::GetScrollY() >= scrollMax - ImGui::GetTextLineHeightWithSpacing();

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0F, 0.0F));
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(visible.size()));
    while (clipper.Step()) {
        for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; row++) {
            const auto* entry = visible[static_cast<size_t>(row)];
            ImGui::TextUnformatted(showHex_ ? HexLine(*entry).c_str() : Describe(*entry).c_str());
        }
    }
    clipper.End();
    ImGui::PopStyleVar();

    if (atBottom) {
        ImGui::SetScrollHereY(1.0F);
    }

    ImGui::EndChild();
}

}  // namespace d2bs::lod114d::console::packet_inspector
