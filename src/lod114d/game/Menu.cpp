#include "game/Menu.h"

#include "components/config/AppConfig.h"
#include "game/Control.h"
#include "game/GameHelpers.h"
#include "game/GameLock.h"
#include "game/GameThread.h"
#include "imports/D2Win.h"
#include "imports/extras/D2WinControlStrc.h"
#include "utils/utils.h"

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmissing-braces"
#include <D2WinTextBox.h>
#pragma clang diagnostic pop

#include <algorithm>
#include <chrono>
#include <optional>
#include <string>
#include <thread>

namespace d2bs::game {

namespace {

using std::chrono::steady_clock;

// One reference: a CONTROL_BUTTON dwDisabled value for "selectable" in the
// difficulty buttons. 0x0D matches reference Profile.cpp:174-188 (the in-game
// "enabled" state of normal/nightmare/hell on the SP difficulty screen).
constexpr uint32_t DIFFICULTY_BUTTON_ENABLED = 0x0D;

imports::extras::D2WinControlStrc* ResolveCtrlPtr(ControlType type, Rect bounds) {
    GameReadLock guard;
    for (auto* p = *imports::d2win::gpFirstControl; p != nullptr; p = p->pNext) {
        if (p->dwType == type && p->rect == bounds) {
            return p;
        }
    }
    return nullptr;
}

bool ClickButtonAt(uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
    auto ctrl = Control::Find(ControlType::Button, x, y, w, h);
    if (!ctrl) {
        return false;
    }
    ctrl->Click();
    return true;
}

bool SetEditBoxText(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const std::string& text) {
    auto ctrl = Control::Find(ControlType::EditBox, x, y, w, h);
    if (!ctrl) {
        return false;
    }
    ctrl->SetText(text);
    return true;
}

struct InputLatch {
    InputLatch() {
        config::GetAppConfig().blockKeys.store(true);
        config::GetAppConfig().blockMouse.store(true);
    }
    ~InputLatch() {
        config::GetAppConfig().blockKeys.store(false);
        config::GetAppConfig().blockMouse.store(false);
    }
    InputLatch(const InputLatch&) = delete;
    InputLatch& operator=(const InputLatch&) = delete;
    InputLatch(InputLatch&&) = delete;
    InputLatch& operator=(InputLatch&&) = delete;
};

// Reference distinguishes "Other Multiplayer" (OpenBnet, TCP/IP) profile types
// that need the MainMenu -> "Other Multiplayer" detour vs ones that hit Bnet
// or Single directly.
bool IsOtherMultiplayer(config::ProfileType type) {
    return type == config::ProfileType::OpenBattleNet || type == config::ProfileType::TcpIpHost ||
           type == config::ProfileType::TcpIpJoin;
}

bool IsTcpIp(config::ProfileType type) {
    return type == config::ProfileType::TcpIpHost || type == config::ProfileType::TcpIpJoin;
}

// Per reference Profile.cpp:248-258 - these locations terminate the loop with
// success (we've reached the lobby/chat/whatever entry that means the menu
// flow is "done driving").
bool IsTerminalSuccess(OutOfGameLocation loc) {
    switch (loc) {
        case OutOfGameLocation::Lobby:
        case OutOfGameLocation::WaitingInLine:
        case OutOfGameLocation::LobbyChat:
        case OutOfGameLocation::LobbyCreateGame:
        case OutOfGameLocation::LobbyJoinGame:
        case OutOfGameLocation::LobbyLadder:
        case OutOfGameLocation::LobbyChannel:
        case OutOfGameLocation::GameAlreadyExists:
        case OutOfGameLocation::GameDoesNotExist:
            return true;
        default:
            return false;
    }
}

}  // namespace

LoginResult Login(const config::ProfileData& profile) {
    InputLatch latch;
    bool skippedToBnet = true;
    bool clickedOnce = false;
    auto start = steady_clock::now();

    while (true) {
        if (GetGameState() == GameState::InGame) {
            return {.status = LoginStatus::Success, .errorMessage = ""};
        }

        // UI helpers (`Control::Find`, `Control::Click`, `SetEditBoxText`,
        // `SelectCharacter`, `SelectGateway`) are individually thread-safe -
        // each acquires the briefest game-thread access it needs and the
        // inter-press sleep in `Click` runs on whichever thread invoked it.
        // Running the dispatch here on the calling thread keeps the per-step
        // sleeps off the game thread.
        std::optional<std::string> step;
        const auto loc = GetOutOfGameLocation();
        if (!IsTerminalSuccess(loc)) {
            switch (loc) {
                case OutOfGameLocation::SplashScreen: {
                    if (auto first = Control::GetFirst()) {
                        first->Click();
                    }
                    break;
                }
                case OutOfGameLocation::CharacterSelect: {
                    if (!SelectCharacter(profile.character)) {
                        step = std::string{"Invalid character name"};
                    }
                    break;
                }
                case OutOfGameLocation::MainMenu: {
                    if (profile.type == config::ProfileType::SinglePlayer) {
                        if (!ClickButtonAt(264, 324, 272, 35)) {
                            step = std::string{"Failed to click the Single button?"};
                            break;
                        }
                    }
                    if (profile.type == config::ProfileType::BattleNet) {
                        if (!profile.gateway.empty()) {
                            SelectGateway(profile.gateway);  // best-effort, mismatched gateway still proceeds
                        }
                        if (!ClickButtonAt(264, 366, 272, 35)) {
                            step = std::string{"Failed to click the 'Battle.net' button?"};
                            break;
                        }
                    }
                    if (IsOtherMultiplayer(profile.type)) {
                        if (!ClickButtonAt(264, 433, 272, 35)) {
                            step = std::string{"Failed to click the 'Other Multiplayer' button?"};
                            break;
                        }
                        skippedToBnet = false;
                    }
                    break;
                }
                case OutOfGameLocation::Login: {
                    if ((profile.type == config::ProfileType::SinglePlayer || IsOtherMultiplayer(profile.type)) &&
                        skippedToBnet) {
                        if (!ClickButtonAt(33, 572, 128, 35)) {
                            step = std::string{"Failed to click the exit button?"};
                        }
                        break;
                    }
                    if (!SetEditBoxText(322, 342, 162, 19, profile.username)) {
                        step = std::string{"Failed to set the 'Username' text-edit box."};
                        break;
                    }
                    if (!SetEditBoxText(322, 396, 162, 19, profile.password)) {
                        step = std::string{"Failed to set the 'Password' text-edit box."};
                        break;
                    }
                    if (!ClickButtonAt(264, 484, 272, 35)) {
                        step = std::string{"Failed to click the 'Log in' button?"};
                    }
                    break;
                }
                case OutOfGameLocation::SelectDifficultySinglePlayer: {
                    auto normal = Control::Find(ControlType::Button, 264, 297, 272, 35);
                    auto nightmare = Control::Find(ControlType::Button, 264, 340, 272, 35);
                    auto hell = Control::Find(ControlType::Button, 264, 383, 272, 35);

                    auto clickIfEnabled = [](const std::optional<Control>& c) {
                        if (!c || c->Disabled() != DIFFICULTY_BUTTON_ENABLED) {
                            return false;
                        }
                        c->Click();
                        return true;
                    };

                    switch (profile.difficulty) {
                        case Difficulty::Normal:
                            if (!clickIfEnabled(normal)) {
                                step = std::string{"Failed to click the 'Normal Difficulty' button?"};
                            }
                            break;
                        case Difficulty::Nightmare:
                            if (!clickIfEnabled(nightmare)) {
                                step = std::string{"Failed to click the 'Nightmare Difficulty' button?"};
                            }
                            break;
                        case Difficulty::Hell:
                            if (!clickIfEnabled(hell)) {
                                step = std::string{"Failed to click the 'Hell Difficulty' button?"};
                            }
                            break;
                        case Difficulty::HighestAvailable:
                            if (!clickIfEnabled(hell) && !clickIfEnabled(nightmare) && !clickIfEnabled(normal)) {
                                step = std::string{"Failed to click ANY difficulty button?"};
                            }
                            break;
                    }
                    break;
                }
                case OutOfGameLocation::OtherMultiplayer: {
                    if (profile.type == config::ProfileType::OpenBattleNet) {
                        if (!ClickButtonAt(264, 310, 272, 35)) {
                            step = std::string{"Failed to click the 'Open Battle.net' button?"};
                            break;
                        }
                    }
                    if (IsTcpIp(profile.type)) {
                        const bool clicked = ClickButtonAt(264, 350, 272, 35);
                        if (!clicked && !clickedOnce) {
                            step = std::string{"Failed to click the 'TCP/IP Game' button?"};
                            break;
                        }
                        if (clicked) {
                            clickedOnce = true;
                        }
                    }
                    break;
                }
                case OutOfGameLocation::TcpIp: {
                    if (profile.type == config::ProfileType::TcpIpHost) {
                        if (!ClickButtonAt(265, 206, 272, 35)) {
                            step = std::string{"Failed to click the 'Host Game' button?"};
                        }
                    }
                    if (profile.type == config::ProfileType::TcpIpJoin) {
                        if (!ClickButtonAt(265, 264, 272, 35)) {
                            step = std::string{"Failed to click the 'Join Game' button?"};
                        }
                    }
                    break;
                }
                case OutOfGameLocation::EnterIpAddress: {
                    if (profile.ip.empty()) {
                        step = std::string{"Could not get the IP address from the profile in the d2bs.ini file."};
                        break;
                    }
                    auto edit = Control::Find(ControlType::EditBox, 300, 268);
                    if (!edit) {
                        step = std::string{"Failed to find the 'Host IP Address' text-edit box."};
                        break;
                    }
                    edit->SetText(profile.ip);
                    if (!ClickButtonAt(421, 337, 96, 32)) {
                        step = std::string{"Failed to click the OK button"};
                    }
                    break;
                }
                case OutOfGameLocation::UnableToConnectTcpIp:
                    step = std::string{"Failed to join Host IP Address"};
                    break;
                case OutOfGameLocation::UnableToConnect:
                    step = std::string{"Unable to connect"};
                    break;
                case OutOfGameLocation::CDKeyInUse:
                    step = std::string{"CD-Key in use"};
                    break;
                case OutOfGameLocation::LoginError:
                    step = std::string{"Bad account or password"};
                    break;
                case OutOfGameLocation::RealmDown:
                    step = std::string{"Realm Down"};
                    break;
                case OutOfGameLocation::MainMenuConnecting:
                case OutOfGameLocation::CharacterSelectPleaseWait:
                case OutOfGameLocation::LobbyPleaseWait:
                case OutOfGameLocation::Gateway:
                case OutOfGameLocation::CharacterSelectNoChars:
                case OutOfGameLocation::Connecting:
                case OutOfGameLocation::PreSplash:
                    // Transient - wait another iteration.
                    break;
                default:
                    step = std::string{"Unhandled login location"};
                    break;
            }
        }

        if (step.has_value()) {
            return {.status = LoginStatus::Error, .errorMessage = std::move(*step)};
        }
        if (GetOutOfGameLocation() == OutOfGameLocation::PreSplash && GetGameState() != GameState::Menu) {
            // Reached one of the success-without-explicit-terminal paths
            // (e.g. dropped into game immediately): treat as success.
            return {.status = LoginStatus::Success, .errorMessage = ""};
        }
        if (IsTerminalSuccess(GetOutOfGameLocation())) {
            return {.status = LoginStatus::Success, .errorMessage = ""};
        }

        if (steady_clock::now() - start >= profile.maxLoginTime) {
            return {.status = LoginStatus::Timeout, .errorMessage = "login time out"};
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

// Reference Control.cpp:240 (`OOG_SelectGateway`). Reads the gateway button's
// current text, compares case-insensitively against `gateway`; if no match,
// opens the dropdown, walks the list textbox to find the matching entry, then
// clicks at the row y-offset and confirms with OK. The "ERROR" early-out
// mirrors reference's defensive bail when callers pass a bogus realm string.
bool SelectGateway(const std::string& gateway) {
    if (gateway.empty() || utils::ContainsCaseInsensitive(gateway, "ERROR")) {
        return false;
    }
    if (GetGameState() != GameState::Menu) {
        return false;
    }

    // Step 1: read current gateway, return the button if a change is needed
    // (or std::nullopt if already correct / not found). The click + sleep
    // intentionally run on the calling thread, not inside the Execute.
    auto gatewayBtn = GameThread::Execute([&]() -> std::optional<Control> {
        auto btn = Control::Find(ControlType::Button, 264, 391, 272, 25);
        if (!btn || utils::ContainsCaseInsensitive(btn->Text(), gateway)) {
            return std::nullopt;
        }
        return btn;
    });
    if (!gatewayBtn) {
        return true;  // already correct, or button not found (same observable effect)
    }
    gatewayBtn->Click();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // Step 2: find the matching gateway row in the list textbox.
    const int32_t matchIndex = GameThread::Execute([&]() -> int32_t {
        GameReadLock guard;
        auto* listBox = ResolveCtrlPtr(ControlType::TextBox,
                                       Rect{.origin = {.x = 257, .y = 500}, .size = {.width = 292, .height = 160}});
        if (listBox == nullptr) {
            return -1;
        }
        int32_t i = 0;
        for (auto* line = listBox->pFirstText; line != nullptr; line = line->pNext, ++i) {
            auto* slot = line->pColumns[0];
            if (slot == nullptr) {
                continue;
            }
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
            auto entry = utils::ToStr(std::wstring{reinterpret_cast<const wchar_t*>(slot)});
            if (utils::ContainsCaseInsensitive(entry, gateway)) {
                return i;
            }
        }
        return -1;
    });

    // Step 3: click the matching row, if any. Resolve the control on the
    // game thread; click + sleep on the calling thread.
    if (matchIndex >= 0) {
        auto listCtrl = GameThread::Execute([] { return Control::Find(ControlType::TextBox, 257, 500, 292, 160); });
        if (listCtrl) {
            // Reference clicks at (-1, 344 + index * 24 + 12) on the textbox -
            // base y at the first row, 24 pixels per row, +12 centers in the row.
            const uint32_t targetY = 344U + (static_cast<uint32_t>(matchIndex) * 24U) + 12U;
            listCtrl->Click(Position{.x = ~0U, .y = targetY});
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    // Step 4: click OK to confirm (or cancel - OK closes the dropdown either way).
    auto okBtn = GameThread::Execute([] { return Control::Find(ControlType::Button, 281, 538, 96, 32); });
    if (okBtn) {
        okBtn->Click();
    }
    return matchIndex >= 0;
}

bool SelectCharacter(const std::string& charName) {
    if (GetGameState() != GameState::Menu) {
        return false;
    }
    // The first character-row textbox sits at (237, 178, 72, 93) and the
    // remaining rows follow on `pNext`. Walk from that anchor and match the
    // second text line's first column against `charName`.
    const std::optional<Control> hit = GameThread::Execute([&]() -> std::optional<Control> {
        for (auto curr = Control::Find(ControlType::TextBox, 237, 178, 72, 93); curr;
             curr = std::optional<Control>{curr->GetNext()}) {
            if (!*curr || curr->Type() != ControlType::TextBox) {
                continue;
            }
            const auto lines = curr->TextLines();
            if (lines.size() < 2 || !lines[1][0]) {
                continue;
            }
            if (utils::EqualsCaseInsensitive(*lines[1][0], charName)) {
                return curr;
            }
        }
        return std::nullopt;
    });
    if (!hit) {
        return false;
    }
    hit->Click();
    return ClickButtonAt(627, 572, 128, 35);
}

bool CreateGame(const std::string& name, const std::string& password, Difficulty difficulty) {
    // Each UI interaction runs in its own GameThread::Execute and we sleep on
    // the calling thread between them. Holding the game write lock across an
    // entire multi-step flow would prevent the menu UI from repainting between
    // clicks - clicks would "fire" against stale screen state.
    if (GetGameState() != GameState::Menu) {
        return false;
    }
    const auto loc = GetOutOfGameLocation();
    const bool acceptableEntry = loc == OutOfGameLocation::Lobby || loc == OutOfGameLocation::LobbyChat ||
                                 loc == OutOfGameLocation::SelectDifficultySinglePlayer ||
                                 loc == OutOfGameLocation::LobbyCreateGame;
    if (!acceptableEntry) {
        return false;
    }

    // Single-player difficulty screen: one tap on the requested button.
    if (loc == OutOfGameLocation::SelectDifficultySinglePlayer) {
        return GameThread::Execute([difficulty]() -> bool {
            const auto pickButton = [](Difficulty d) -> std::optional<Position> {
                switch (d) {
                    case Difficulty::Normal:
                        return Position{.x = 264, .y = 297};
                    case Difficulty::Nightmare:
                        return Position{.x = 264, .y = 340};
                    case Difficulty::Hell:
                        return Position{.x = 264, .y = 383};
                    case Difficulty::HighestAvailable:
                        return std::nullopt;
                }
                return std::nullopt;
            };
            const auto attempt = [](uint32_t x, uint32_t y) -> bool {
                auto c = Control::Find(ControlType::Button, x, y, 272, 35);
                if (!c || c->Disabled() != DIFFICULTY_BUTTON_ENABLED) {
                    return false;
                }
                c->Click();
                return true;
            };
            if (auto pos = pickButton(difficulty)) {
                return attempt(pos->x, pos->y);
            }
            // HighestAvailable: try Hell, then Nightmare, then Normal.
            return attempt(264, 383) || attempt(264, 340) || attempt(264, 297);
        });
    }

    // Lobby flow: navigate to the Create-Game tab if not already there.
    if (loc != OutOfGameLocation::LobbyCreateGame) {
        if (!GameThread::Execute([]() -> bool { return ClickButtonAt(533, 469, 120, 20); })) {
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    // If the click landed but the location hasn't repainted to LobbyCreateGame
    // yet (within the 100ms sleep above), reference (Control.cpp:491) returns
    // TRUE best-effort instead of FALSE - the click already fired, the form
    // will fill on the next iteration if needed.
    if (GetOutOfGameLocation() != OutOfGameLocation::LobbyCreateGame) {
        return true;
    }
    if (!GameThread::Execute([&]() -> bool { return SetEditBoxText(432, 162, 158, 20, name); })) {
        return false;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    if (!GameThread::Execute([&]() -> bool { return SetEditBoxText(432, 217, 158, 20, password); })) {
        return false;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Bnet difficulty radio buttons: distinct dwDisabled value (0x04 means
    // "unavailable"; anything else is selectable).
    constexpr uint32_t BNET_DIFF_UNAVAILABLE = 0x04;
    const auto pickBnetButton = [](Difficulty d) -> std::optional<Position> {
        switch (d) {
            case Difficulty::Normal:
                return Position{.x = 430, .y = 381};
            case Difficulty::Nightmare:
                return Position{.x = 555, .y = 381};
            case Difficulty::Hell:
                return Position{.x = 698, .y = 381};
            case Difficulty::HighestAvailable:
                return std::nullopt;
        }
        return std::nullopt;
    };
    const bool diffOk = GameThread::Execute([&]() -> bool {
        const auto tryBnet = [](uint32_t x, uint32_t y) -> bool {
            auto c = Control::Find(ControlType::Button, x, y, 16, 16);
            if (!c || c->Disabled() == BNET_DIFF_UNAVAILABLE) {
                return false;
            }
            c->Click();
            return true;
        };
        if (auto pos = pickBnetButton(difficulty)) {
            return tryBnet(pos->x, pos->y);
        }
        return tryBnet(698, 381) || tryBnet(555, 381) || tryBnet(430, 381);
    });
    if (!diffOk) {
        return false;
    }
    return GameThread::Execute([]() -> bool { return ClickButtonAt(594, 433, 172, 32); });
}

bool JoinGame(const std::string& name, const std::string& password) {
    // Each UI interaction runs in its own GameThread::Execute and we sleep on
    // the calling thread between them, so the menu UI can repaint between
    // clicks.
    if (GetGameState() != GameState::Menu) {
        return false;
    }
    const auto loc = GetOutOfGameLocation();
    const bool acceptableEntry = loc == OutOfGameLocation::Lobby || loc == OutOfGameLocation::LobbyChat ||
                                 loc == OutOfGameLocation::LobbyJoinGame;
    if (!acceptableEntry) {
        return false;
    }
    if (loc != OutOfGameLocation::LobbyJoinGame) {
        if (!GameThread::Execute([]() -> bool { return ClickButtonAt(652, 469, 120, 20); })) {
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    // Per reference Control.cpp:569 - if the click landed but the location
    // hasn't repainted to LobbyJoinGame yet, return TRUE best-effort instead
    // of FALSE.
    if (GetOutOfGameLocation() != OutOfGameLocation::LobbyJoinGame) {
        return true;
    }
    if (!GameThread::Execute([&]() -> bool { return SetEditBoxText(432, 148, 155, 20, name); })) {
        return false;
    }
    if (!GameThread::Execute([&]() -> bool { return SetEditBoxText(606, 148, 155, 20, password); })) {
        return false;
    }
    return GameThread::Execute([]() -> bool { return ClickButtonAt(594, 433, 172, 32); });
}

bool CreateCharacter(const std::string& name, CharacterClass charClass, bool /*isHardcore*/, bool /*isLadder*/) {
    // Each UI interaction runs in its own GameThread::Execute and we sleep /
    // poll on the calling thread between them. Holding the game write lock
    // across the multi-step flow would prevent the menu UI from repainting
    // between clicks - the polling loop below would see stale state forever.
    if (GetOutOfGameLocation() != OutOfGameLocation::CharacterSelect) {
        return false;
    }
    if (name.size() > 15) {
        return false;
    }
    // Click the "Create New Character" button.
    if (!GameThread::Execute([]() -> bool { return ClickButtonAt(33, 528, 168, 60); })) {
        return false;
    }

    // Wait for char-create screen to come up.
    for (int32_t i = 0; i < 30 && GetOutOfGameLocation() != OutOfGameLocation::CharacterCreate; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    if (GetOutOfGameLocation() != OutOfGameLocation::CharacterCreate) {
        return false;
    }

    // Reference Control.cpp:148-156 - per-class image control and the
    // double-click coordinates used to "select then confirm" the class.
    struct ClassEntry {
        uint32_t imgX;
        uint32_t imgY;
        uint32_t clickX;
        uint32_t clickY;
    };
    constexpr std::array<ClassEntry, 7> CLASS_ENTRIES = {{
        {.imgX = 100, .imgY = 337, .clickX = 80, .clickY = 330},   // Amazon
        {.imgX = 626, .imgY = 353, .clickX = 600, .clickY = 300},  // Sorceress
        {.imgX = 301, .imgY = 333, .clickX = 300, .clickY = 330},  // Necromancer
        {.imgX = 521, .imgY = 339, .clickX = 500, .clickY = 330},  // Paladin
        {.imgX = 400, .imgY = 330, .clickX = 390, .clickY = 330},  // Barbarian
        {.imgX = 720, .imgY = 370, .clickX = 700, .clickY = 370},  // Druid
        {.imgX = 232, .imgY = 364, .clickX = 200, .clickY = 300},  // Assassin
    }};

    const auto idx = static_cast<size_t>(charClass);
    if (idx >= CLASS_ENTRIES.size()) {
        return false;
    }
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index) - bounded by .size() above
    const auto& entry = CLASS_ENTRIES[idx];
    const Position clickAt{.x = entry.clickX, .y = entry.clickY};
    const bool clickedFirst = GameThread::Execute([&]() -> bool {
        auto img = Control::Find(ControlType::Image, entry.imgX, entry.imgY, 88, 184);
        if (!img) {
            return false;
        }
        img->Click(clickAt);
        return true;
    });
    if (!clickedFirst) {
        return false;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    GameThread::Execute([&]() {
        auto img = Control::Find(ControlType::Image, entry.imgX, entry.imgY, 88, 184);
        if (img) {
            img->Click(clickAt);
        }
    });

    // TODO(implement): type the character name into the
    // name editbox and click OK. Reference itself bails here ("still need to
    // find the name editbox..."). Without a confirmed control rect for the
    // name field on 1.14d we cannot wire it without a runtime probe.
    return false;
}

OutOfGameLocation GetOutOfGameLocation() {
    if (GetGameState() != GameState::Menu) {
        return OutOfGameLocation::PreSplash;
    }

    auto haveButton = [](uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
        return Control::Find(ControlType::Button, x, y, w, h).has_value();
    };
    auto haveButtonWithLocale = [](int32_t localeId, uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
        return Control::Find(ControlType::Button, x, y, w, h, localeId).has_value();
    };
    auto haveTextBox = [](uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
        return Control::Find(ControlType::TextBox, x, y, w, h).has_value();
    };
    auto haveTextBoxWithLocale = [](int32_t localeId, uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
        return Control::Find(ControlType::TextBox, x, y, w, h, localeId).has_value();
    };
    auto haveAnyControlExceptControlsByType = [](ControlType t, uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
        return Control::Find(t, x, y, w, h).has_value();
    };

    // Reference Control.cpp:323-336 - Locale 5102 (OK) at (351,337,96,32) selects
    // the "OK" button arm; 5103 (Cancel) selects the Cancel arm. The port can now
    // disambiguate via the new localeId filter on Control::Find.
    constexpr int32_t LOCALE_OK = 5102;
    constexpr int32_t LOCALE_CANCEL = 5103;

    if (haveButton(330, 416, 128, 35)) {
        return OutOfGameLocation::MainMenuConnecting;
    }
    if (haveButton(335, 412, 128, 35)) {
        return OutOfGameLocation::LoginError;
    }
    if (haveButtonWithLocale(LOCALE_OK, 351, 337, 96, 32)) {
        if (haveTextBoxWithLocale(5351, 268, 320, 264, 120)) {
            return OutOfGameLocation::LostConnection;
        }
        if (haveTextBoxWithLocale(5347, 268, 320, 264, 120)) {
            return OutOfGameLocation::Disconnected;
        }
        if (haveAnyControlExceptControlsByType(ControlType::Button, 265, 206, 272, 35)) {
            return OutOfGameLocation::UnableToConnectTcpIp;
        }
        return OutOfGameLocation::CharacterCreateAlreadyExists;
    }
    if (haveButtonWithLocale(LOCALE_CANCEL, 351, 337, 96, 32)) {
        if (haveTextBoxWithLocale(5243, 268, 300, 264, 100)) {
            return OutOfGameLocation::CharacterSelectPleaseWait;
        }
        if (haveTextBox(268, 320, 264, 120)) {
            return OutOfGameLocation::LobbyPleaseWait;
        }
    }
    if (haveButton(433, 433, 96, 32)) {
        if (haveTextBox(427, 234, 300, 100)) {
            return OutOfGameLocation::WaitingInLine;
        }
        if (haveTextBox(459, 380, 150, 12)) {
            return OutOfGameLocation::LobbyCreateGame;
        }
        if (haveButton(594, 433, 172, 32)) {
            return OutOfGameLocation::LobbyJoinGame;
        }
        if (haveButton(671, 433, 96, 32)) {
            return OutOfGameLocation::LobbyChannel;
        }
        return OutOfGameLocation::LobbyLadder;
    }
    if (haveButton(33, 572, 128, 35)) {
        if (haveButton(264, 484, 272, 35)) {
            return OutOfGameLocation::Login;
        }
        if (haveButton(495, 438, 96, 32)) {
            return OutOfGameLocation::CharacterSelectChangeRealm;
        }
        if (haveButton(627, 572, 128, 35) && haveButton(33, 528, 168, 60)) {
            if (haveButton(264, 297, 272, 35)) {
                return OutOfGameLocation::SelectDifficultySinglePlayer;
            }
            auto namePane = Control::Find(ControlType::TextBox, 37, 178, 200, 92);
            if (namePane) {
                const auto lines = namePane->TextLines();
                if (lines.size() >= 2) {
                    return OutOfGameLocation::CharacterSelect;
                }
            }
            if (haveTextBox(45, 318, 531, 140)) {
                return OutOfGameLocation::RealmDown;
            }
            if (haveTextBox(45, 318, 531, 140)) {
                return OutOfGameLocation::Connecting;
            }
            return OutOfGameLocation::CharacterSelectNoChars;
        }
        if (haveButton(33, 572, 128, 35)) {
            if (auto okBtn = Control::Find(ControlType::Button, 627, 572, 128, 35);
                okBtn && okBtn->Disabled() == 0 && okBtn->IsAvailable()) {
                return OutOfGameLocation::CharacterCreateClassSelected;
            }
            if (haveTextBox(321, 448, 300, 32)) {
                return OutOfGameLocation::NewAccount;
            }
            return OutOfGameLocation::CharacterCreate;
        }
    }
    if (haveButton(335, 450, 128, 35)) {
        if (haveTextBox(162, 270, 477, 50)) {
            return OutOfGameLocation::CDKeyInUse;
        }
        if (haveTextBox(162, 420, 477, 100)) {
            return OutOfGameLocation::UnableToConnect;
        }
        return OutOfGameLocation::InvalidCDKey;
    }
    if (haveTextBoxWithLocale(5159, 438, 300, 326, 150)) {
        return OutOfGameLocation::GameDoesNotExist;
    }
    if (haveTextBoxWithLocale(5161, 438, 300, 326, 150)) {
        return OutOfGameLocation::GameIsFull;
    }
    if (haveTextBoxWithLocale(5138, 438, 300, 326, 150)) {
        return OutOfGameLocation::GameAlreadyExists;
    }
    if (haveTextBoxWithLocale(5139, 438, 300, 326, 150)) {
        return OutOfGameLocation::ServerDown;
    }
    if (haveButton(264, 324, 272, 35)) {
        return OutOfGameLocation::MainMenu;
    }
    if (haveButton(27, 480, 120, 20)) {
        return OutOfGameLocation::Lobby;
    }
    if (haveButton(187, 470, 80, 20)) {
        return OutOfGameLocation::LobbyChat;
    }
    if (haveTextBox(100, 580, 600, 80)) {
        return OutOfGameLocation::SplashScreen;
    }
    if (haveButton(281, 538, 96, 32)) {
        return OutOfGameLocation::Gateway;
    }
    if (haveButtonWithLocale(5102, 525, 513, 128, 35)) {
        return OutOfGameLocation::PleaseRead;
    }
    if (haveButtonWithLocale(5181, 525, 513, 128, 35)) {
        return OutOfGameLocation::AgreeToTerms;
    }
    if (haveButton(265, 527, 272, 35)) {
        return OutOfGameLocation::RegisterEmail;
    }
    if (haveButton(33, 578, 128, 35)) {
        return OutOfGameLocation::Credits;
    }
    if (haveButton(334, 488, 128, 35)) {
        return OutOfGameLocation::Cinematics;
    }
    if (haveButton(264, 350, 272, 35)) {
        return OutOfGameLocation::OtherMultiplayer;
    }
    if (haveButton(281, 337, 96, 32)) {
        return OutOfGameLocation::EnterIpAddress;
    }
    if (haveButton(265, 206, 272, 35)) {
        return OutOfGameLocation::TcpIp;
    }
    return OutOfGameLocation::PreSplash;
}

}  // namespace d2bs::game
