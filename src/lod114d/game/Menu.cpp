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
#include <utility>

namespace d2bs::game {

namespace {

// One reference: a CONTROL_BUTTON dwState value for "selectable" in the
// difficulty buttons. 0x0D matches reference Profile.cpp:174-188 (the in-game
// "enabled" state of normal/nightmare/hell on the SP difficulty screen).
constexpr uint32_t DIFFICULTY_BUTTON_ENABLED = 0x0D;

// D2 string-table (.tbl) indices passed to Control::Find as its localeId filter,
// mirroring the reference OOG_GetLocation's per-control labels: the id is
// resolved to its locale string and matched against the control's text (Button
// exact, TextBox substring). The few controls the reference matches by rect
// alone (NULL text) pass no label - those call sites are commented. Values
// cross-checked against D2MOO's DataTbls/StringIds.h and the string tables;
// trailing text is the resolved English. Index space by base offset: string.tbl
// 0, patchstring.tbl 10000, expansionstring.tbl 20000.
enum class StringId : int32_t {
    // string.tbl
    Exit = 5101,                // "EXIT"
    Ok = 5102,                  // "OK"
    Cancel = 5103,              // "CANCEL"
    SinglePlayer = 5106,        // "SINGLE PLAYER"
    TcpIpGame = 5116,           // "TCP/IP GAME"
    HostGame = 5118,            // "HOST GAME"
    JoinGame = 5119,            // "JOIN GAME"
    GameExistsWithName = 5138,  // "A Game Already Exists With That Name"
    ServerDown = 5139,          // "Server Down"
    GameDoesNotExist = 5159,    // "Game does not exist."
    GameIsFull = 5161,          // "Game is Full."
    Agree = 5181,               // "AGREE"
    ModemConnectHelp = 5190,    // "If using a modem, you may need to connect ..."
    CdKeyInUse = 5200,          // "Your CD key is currently being used by:"
    VerifyPassword = 5226,      // "Verify Password"
    PleaseWait = 5243,          // "Please Wait"
    LogIn = 5288,               // "LOG IN"
    Help = 5308,                // "HELP"
    Disconnected = 5347,        // "You were disconnected from battle.net. ..."
    LostConnection = 5351,      // "Lost Connection to battle.net."
    // patchstring.tbl (base 10000)
    Normal = 10018,            // "NORMAL"
    CreateNew = 10832,         // "CREATE NEW"
    Connecting = 11065,        // "CONNECTING..."
    RealmUnavailable = 11066,  // "Diablo II was unable to connect to the realm server ..."
    Register = 11097,          // "REGISTER"
    EnterChat = 11126,         // "ENTER CHAT"
    RealmRestricted = 11162,   // "Your connection has been temporarily restricted ..."
    // expansionstring.tbl (base 20000)
    Copyright = 21882,  // " Copyright 2001 Blizzard Entertainment"
};

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

// Mirrors reference Profile::login's timeout accounting
// (reference/d2bs/Profile.cpp:164,246): only iterations spent at the login screen
// or waiting on the server (connecting / "please wait" / gateway / char-list load)
// are charged against maxLoginTime. Time spent clicking through action screens
// (main menu, char select, difficulty, ...) is not, so a login that is actively
// progressing - just slowly - never throws "login time out"; only one genuinely
// stuck waiting does. These are exactly the locations the Login() switch treats as
// transient, plus the login screen itself.
bool IsLoginWaitState(OutOfGameLocation loc) {
    switch (loc) {
        case OutOfGameLocation::Login:
        case OutOfGameLocation::MainMenuConnecting:
        case OutOfGameLocation::CharacterSelectPleaseWait:
        case OutOfGameLocation::LobbyPleaseWait:
        case OutOfGameLocation::Gateway:
        case OutOfGameLocation::CharacterSelectNoChars:
        case OutOfGameLocation::Connecting:
        case OutOfGameLocation::PreSplash:
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
    // Poll cadence for the loop, and the time charged against maxLoginTime per
    // wait iteration (see IsLoginWaitState).
    constexpr auto LOGIN_POLL_INTERVAL = std::chrono::milliseconds{100};
    auto loginWaitElapsed = std::chrono::milliseconds::zero();

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
                        if (!c || c->State() != DIFFICULTY_BUTTON_ENABLED) {
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

        // Charge only "waiting on the server / at login" time against
        // maxLoginTime, matching reference Profile::login
        // (reference/d2bs/Profile.cpp:282). Time spent clicking through action
        // screens is not counted, so a slow-but-progressing login does not
        // spuriously throw "login time out".
        if (IsLoginWaitState(loc)) {
            loginWaitElapsed += LOGIN_POLL_INTERVAL;
        }
        if (loginWaitElapsed > profile.maxLoginTime) {
            return {.status = LoginStatus::Timeout, .errorMessage = "login time out"};
        }
        std::this_thread::sleep_for(LOGIN_POLL_INTERVAL);
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
                if (!c || c->State() != DIFFICULTY_BUTTON_ENABLED) {
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

    // Bnet difficulty radio buttons: distinct dwState value (0x04 means
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
            if (!c || c->State() == BNET_DIFF_UNAVAILABLE) {
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

    // Identify the out-of-game screen by which menu control(s) are present.
    // Mirrors the reference OOG_GetLocation (reference/d2bs/Control.cpp): the
    // if / else-if structure and fall-through match it so screens that share a
    // control rect stay distinguished. findControl is a present-check by type +
    // rect, with an optional StringId label for the screens that need one to
    // disambiguate (see the StringId comment for which controls those are); the
    // rest match on rect alone. namePane and the char-create OK button are read
    // via Control::Find directly because they need the control, not just presence.
    auto findControl = [](ControlType type, uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                          std::optional<StringId> label = std::nullopt) {
        std::optional<int32_t> localeId;
        if (label) {
            localeId = std::to_underlying(*label);
        }
        return Control::Find(type, x, y, w, h, localeId).has_value();
    };

    // "Connecting to Battle.net": a lone Cancel button.
    if (findControl(ControlType::Button, 330, 416, 128, 35, StringId::Cancel)) {
        return OutOfGameLocation::MainMenuConnecting;
    }
    // Login error popup: a lone OK button.
    if (findControl(ControlType::Button, 335, 412, 128, 35, StringId::Ok)) {
        return OutOfGameLocation::LoginError;
    }
    // Centered-OK dialog: connection/disconnection notices and the duplicate
    // character-name popup share this OK button, told apart by their textbox.
    if (findControl(ControlType::Button, 351, 337, 96, 32, StringId::Ok)) {
        if (findControl(ControlType::TextBox, 268, 320, 264, 120, StringId::LostConnection)) {
            return OutOfGameLocation::LostConnection;
        }
        if (findControl(ControlType::TextBox, 268, 320, 264, 120, StringId::Disconnected)) {
            return OutOfGameLocation::Disconnected;
        }
        // Reference matches any button at the TCP/IP host slot here (no label).
        if (findControl(ControlType::Button, 265, 206, 272, 35)) {
            return OutOfGameLocation::UnableToConnectTcpIp;
        }
        return OutOfGameLocation::CharacterCreateAlreadyExists;
    }
    // Centered-Cancel dialog: "please wait" overlays (char-select vs lobby).
    if (findControl(ControlType::Button, 351, 337, 96, 32, StringId::Cancel)) {
        if (findControl(ControlType::TextBox, 268, 300, 264, 100, StringId::PleaseWait)) {
            return OutOfGameLocation::CharacterSelectPleaseWait;
        }
        // Reference matches this textbox by rect alone (no label).
        if (findControl(ControlType::TextBox, 268, 320, 264, 120)) {
            return OutOfGameLocation::LobbyPleaseWait;
        }
    } else if (findControl(ControlType::Button, 433, 433, 96, 32, StringId::Cancel)) {
        // Battle.net lobby game list (Cancel/back button present). Reference
        // matches the waiting-in-line textbox by rect alone (no label).
        if (findControl(ControlType::TextBox, 427, 234, 300, 100)) {
            return OutOfGameLocation::WaitingInLine;
        }
        if (findControl(ControlType::TextBox, 459, 380, 150, 12, StringId::Normal)) {
            return OutOfGameLocation::LobbyCreateGame;
        }
        if (findControl(ControlType::Button, 594, 433, 172, 32, StringId::JoinGame)) {
            return OutOfGameLocation::LobbyJoinGame;
        }
        if (findControl(ControlType::Button, 671, 433, 96, 32, StringId::Ok)) {
            return OutOfGameLocation::LobbyChannel;
        }
        return OutOfGameLocation::LobbyLadder;
    } else if (findControl(ControlType::Button, 33, 572, 128, 35, StringId::Exit)) {
        // Screens with an Exit button at the bottom-left: login, character
        // select, single-player difficulty, and character create.
        if (findControl(ControlType::Button, 264, 484, 272, 35, StringId::LogIn)) {
            return OutOfGameLocation::Login;
        }
        if (findControl(ControlType::Button, 495, 438, 96, 32, StringId::Ok)) {
            return OutOfGameLocation::CharacterSelectChangeRealm;
        }
        // Character-select screen: an OK button plus the "Create New" button.
        if (findControl(ControlType::Button, 627, 572, 128, 35, StringId::Ok) &&
            findControl(ControlType::Button, 33, 528, 168, 60, StringId::CreateNew)) {
            // Single-player difficulty dialog (Normal/Nightmare/Hell buttons).
            if (findControl(ControlType::Button, 264, 297, 272, 35, StringId::Normal)) {
                return OutOfGameLocation::SelectDifficultySinglePlayer;
            }
            // Name pane with >= 2 text lines (reference: pFirstText->pNext) means
            // characters are listed. Reference matches it by rect alone (no label).
            auto namePane = Control::Find(ControlType::TextBox, 37, 178, 200, 92);
            if (namePane && namePane->TextLines().size() >= 2) {
                return OutOfGameLocation::CharacterSelect;
            }
            // Same rect, different message: realm unavailable / restricted vs the
            // "connecting" notice - disambiguated by the textbox's locale text.
            if (findControl(ControlType::TextBox, 45, 318, 531, 140, StringId::RealmUnavailable) ||
                findControl(ControlType::TextBox, 45, 318, 531, 140, StringId::RealmRestricted)) {
                return OutOfGameLocation::RealmDown;
            }
            if (findControl(ControlType::TextBox, 45, 318, 531, 140, StringId::Connecting)) {
                return OutOfGameLocation::Connecting;
            }
            return OutOfGameLocation::CharacterSelectNoChars;
        }
        if (findControl(ControlType::Button, 33, 572, 128, 35, StringId::Exit)) {
            // Char-create OK button dwState: 0 = hidden (no class picked), 4 =
            // greyed (class picked, no name), 5 = clickable. Hidden is the entry
            // screen (reference CHARACTER_CREATE); any visible state means a class
            // is picked. New-account's OK is visible (5), so its verify-password
            // textbox is checked after this miss.
            if (auto okBtn = Control::Find(ControlType::Button, 627, 572, 128, 35, std::to_underlying(StringId::Ok));
                okBtn && okBtn->State() == 0) {
                return OutOfGameLocation::CharacterCreate;
            }
            if (findControl(ControlType::TextBox, 321, 448, 300, 32, StringId::VerifyPassword)) {
                return OutOfGameLocation::NewAccount;
            }
            return OutOfGameLocation::CharacterCreateClassSelected;
        }
    }

    // CD-key / connection error dialog (OK button), told apart by the textbox.
    if (findControl(ControlType::Button, 335, 450, 128, 35, StringId::Ok)) {
        if (findControl(ControlType::TextBox, 162, 270, 477, 50, StringId::CdKeyInUse)) {
            return OutOfGameLocation::CDKeyInUse;
        }
        if (findControl(ControlType::TextBox, 162, 420, 477, 100, StringId::ModemConnectHelp)) {
            return OutOfGameLocation::UnableToConnect;
        }
        return OutOfGameLocation::InvalidCDKey;
    }
    // Game-listing result popups (438,300 textbox), distinguished by message.
    if (findControl(ControlType::TextBox, 438, 300, 326, 150, StringId::GameDoesNotExist)) {
        return OutOfGameLocation::GameDoesNotExist;
    }
    if (findControl(ControlType::TextBox, 438, 300, 326, 150, StringId::GameIsFull)) {
        return OutOfGameLocation::GameIsFull;
    }
    if (findControl(ControlType::TextBox, 438, 300, 326, 150, StringId::GameExistsWithName)) {
        return OutOfGameLocation::GameAlreadyExists;
    }
    if (findControl(ControlType::TextBox, 438, 300, 326, 150, StringId::ServerDown)) {
        return OutOfGameLocation::ServerDown;
    }
    // Main menu: "SINGLE PLAYER" button.
    if (findControl(ControlType::Button, 264, 324, 272, 35, StringId::SinglePlayer)) {
        return OutOfGameLocation::MainMenu;
    }
    // Battle.net lobby: "ENTER CHAT" button.
    if (findControl(ControlType::Button, 27, 480, 120, 20, StringId::EnterChat)) {
        return OutOfGameLocation::Lobby;
    }
    // Chat room: "HELP" button.
    if (findControl(ControlType::Button, 187, 470, 80, 20, StringId::Help)) {
        return OutOfGameLocation::LobbyChat;
    }
    // Splash screen: the copyright textbox.
    if (findControl(ControlType::TextBox, 100, 580, 600, 80, StringId::Copyright)) {
        return OutOfGameLocation::SplashScreen;
    }
    // Gateway selection: OK button.
    if (findControl(ControlType::Button, 281, 538, 96, 32, StringId::Ok)) {
        return OutOfGameLocation::Gateway;
    }
    // Terms of use: "AGREE" and "please read" (OK) share this rect, so both
    // dialog buttons need their label.
    if (findControl(ControlType::Button, 525, 513, 128, 35, StringId::Agree)) {
        return OutOfGameLocation::AgreeToTerms;
    }
    if (findControl(ControlType::Button, 525, 513, 128, 35, StringId::Ok)) {
        return OutOfGameLocation::PleaseRead;
    }
    // Register email: "REGISTER" button.
    if (findControl(ControlType::Button, 265, 527, 272, 35, StringId::Register)) {
        return OutOfGameLocation::RegisterEmail;
    }
    // Credits: Exit button at a distinct rect.
    if (findControl(ControlType::Button, 33, 578, 128, 35, StringId::Exit)) {
        return OutOfGameLocation::Credits;
    }
    // Cinematics: Cancel button.
    if (findControl(ControlType::Button, 334, 488, 128, 35, StringId::Cancel)) {
        return OutOfGameLocation::Cinematics;
    }
    // Other Multiplayer: "TCP/IP GAME" button.
    if (findControl(ControlType::Button, 264, 350, 272, 35, StringId::TcpIpGame)) {
        return OutOfGameLocation::OtherMultiplayer;
    }
    // Enter IP address: Cancel button.
    if (findControl(ControlType::Button, 281, 337, 96, 32, StringId::Cancel)) {
        return OutOfGameLocation::EnterIpAddress;
    }
    // TCP/IP host/join chooser: "HOST GAME" button.
    if (findControl(ControlType::Button, 265, 206, 272, 35, StringId::HostGame)) {
        return OutOfGameLocation::TcpIp;
    }

    return OutOfGameLocation::PreSplash;
}

}  // namespace d2bs::game
