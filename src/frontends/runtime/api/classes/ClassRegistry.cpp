#include "ClassRegistry.h"

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>
#include <tuple>
#include <utility>

#include "api/core/Convert.h"
#include "components/gameloop/GameLoop.h"
#include "config/AppConfig.h"
#include "game/Constants.h"
#include "game/GameHelpers.h"
#include "game/Unit.h"
#include "unibind/unibind.h"

#include "drawing/JSBox.h"
#include "drawing/JSFrame.h"
#include "drawing/JSImage.h"
#include "drawing/JSLine.h"
#include "drawing/JSText.h"
#include "game/JSArea.h"
#include "game/JSControl.h"
#include "game/JSExit.h"
#include "game/JSParty.h"
#include "game/JSPresetUnit.h"
#include "game/JSRoom.h"
#include "game/JSStashTab.h"
#include "game/JSTxtTables.h"
#include "game/JSUnit.h"
#include "io/JSDBStatement.h"
#include "io/JSDirectory.h"
#include "io/JSFile.h"
#include "io/JSFileTools.h"
#include "io/JSHttpClient.h"
#include "io/JSSQLite.h"
#include "io/JSSocket.h"
#include "scripting/JSCompatibility.h"
#include "scripting/JSProfile.h"
#include "scripting/JSSandbox.h"
#include "scripting/JSScript.h"

namespace d2bs::api::classes {

namespace {

template <class Class>
bool Install(const ub::Context& context, std::string_view name) {
    auto constructor = Class::Get(context.GetIsolate()).GetConstructor(context);
    return constructor && context.GlobalObject().Set(context, name, *constructor).value_or(false);
}

}  // namespace

bool RegisterAllClasses(const ub::Context& context) {
    return
        // Game objects
        Install<JSUnit>(context, "Unit") && Install<JSRoom>(context, "Room") && Install<JSArea>(context, "Area") &&
        Install<JSExit>(context, "Exit") && Install<JSStashTab>(context, "StashTab") &&
        Install<JSPresetUnit>(context, "PresetUnit") && Install<JSParty>(context, "Party") &&
        Install<JSControl>(context, "Control") &&

        // File I/O
        Install<JSFile>(context, "File") && Install<JSFileTools>(context, "FileTools") &&
        Install<JSDirectory>(context, "Folder") &&

        // Drawing
        Install<JSFrame>(context, "Frame") && Install<JSBox>(context, "Box") && Install<JSLine>(context, "Line") &&
        Install<JSText>(context, "Text") && Install<JSImage>(context, "Image") &&

        // Script management
        Install<JSScript>(context, "D2BSScript") && Install<JSSandbox>(context, "Sandbox") &&
        Install<JSProfile>(context, "Profile") && Install<JSCompatibility>(context, "Compatibility") &&

        // Network/DB
        Install<JSHttpClient>(context, "HttpClient") && Install<JSSocket>(context, "Socket") &&
        Install<JSSQLite>(context, "SQLite") && Install<JSDBStatement>(context, "DBStatement") &&

        // Game data tables (static namespace; not constructable)
        Install<JSTxtTables>(context, "TxtTables");
}

void ClearAllClassCaches() {
    // Game objects
    JSUnit::ClearCache();
    JSRoom::ClearCache();
    JSArea::ClearCache();
    JSExit::ClearCache();
    JSStashTab::ClearCache();
    JSPresetUnit::ClearCache();
    JSParty::ClearCache();
    JSControl::ClearCache();

    // File I/O
    JSFile::ClearCache();
    JSFileTools::ClearCache();
    JSDirectory::ClearCache();

    // Drawing
    JSFrame::ClearCache();
    JSBox::ClearCache();
    JSLine::ClearCache();
    JSText::ClearCache();
    JSImage::ClearCache();

    // Script management
    JSScript::ClearCache();
    JSSandbox::ClearCache();
    JSProfile::ClearCache();
    JSCompatibility::ClearCache();

    // Network/DB
    JSHttpClient::ClearCache();
    JSSocket::ClearCache();
    JSSQLite::ClearCache();
    JSDBStatement::ClearCache();

    // Game data tables
    JSTxtTables::ClearCache();
}

std::optional<ub::Local<ub::Object>> CreateMeObject(const ub::Context& context) {
    // 'me': special Unit handle that always resolves to the current player unit.

    // unitId=0, type=0 sentinel: ResolvePtr() returns GetPlayerUnit().
    auto me = JSUnit::Wrap(context, std::make_shared<game::Unit>(game::Unit::Player()));
    if (!me) {
        return std::nullopt;
    }

    /// @description Battle.net / login account name for the current session. Empty string when not logged in.
    /// @type {string}
    JSUnit::InstanceProperty(
        context, *me, "account", +[](const ub::Local<ub::Name>&, const ub::PropertyCallbackInfo& info) {
            std::ignore = info.GetReturnValue().Set(game::GetAccountName());
        });

    /// @description The current player character's name. Empty string when out of game.
    /// @type {string}
    JSUnit::InstanceProperty(
        context, *me, "charname", +[](const ub::Local<ub::Name>&, const ub::PropertyCallbackInfo& info) {
            std::ignore = info.GetReturnValue().Set(game::GetPlayerName());
        });

    /// @description Current game difficulty.
    /// @type {Difficulty}
    JSUnit::InstanceProperty(
        context, *me, "diff", +[](const ub::Local<ub::Name>&, const ub::PropertyCallbackInfo& info) {
            info.GetReturnValue().Set(static_cast<uint32_t>(game::GetDifficulty()));
        });

    /// @description Highest difficulty unlocked for this character.
    /// @type {Difficulty}
    JSUnit::InstanceProperty(
        context, *me, "maxdiff", +[](const ub::Local<ub::Name>&, const ub::PropertyCallbackInfo& info) {
            info.GetReturnValue().Set(static_cast<uint32_t>(game::GetMaxDiff()));
        });

    /// @description Name of the joined/created game. Empty string when not in a game.
    /// @type {string}
    JSUnit::InstanceProperty(
        context, *me, "gamename", +[](const ub::Local<ub::Name>&, const ub::PropertyCallbackInfo& info) {
            std::ignore = info.GetReturnValue().Set(game::GetGameName());
        });

    /// @description Password of the joined/created game. Empty string when not in a game or no password set.
    /// @type {string}
    JSUnit::InstanceProperty(
        context, *me, "gamepassword", +[](const ub::Local<ub::Name>&, const ub::PropertyCallbackInfo& info) {
            std::ignore = info.GetReturnValue().Set(game::GetGamePassword());
        });

    /// @description IP address of the game server for the current session. Empty string when not connected.
    /// @type {string}
    JSUnit::InstanceProperty(
        context, *me, "gameserverip", +[](const ub::Local<ub::Name>&, const ub::PropertyCallbackInfo& info) {
            std::ignore = info.GetReturnValue().Set(game::GetGameServerIp());
        });

    /// @description Tick (milliseconds, same domain as getTickCount()) marking when the current game started, for
    /// computing elapsed game time. 0 when out of game.
    /// @type {number}
    JSUnit::InstanceProperty(
        context, *me, "gamestarttime", +[](const ub::Local<ub::Name>&, const ub::PropertyCallbackInfo& info) {
            // Game-start anchor as steady_clock epoch ms, matching the
            // getTickCount() domain so script comparisons against it hold;
            // 0 when out of game.
            auto anchor = runtime::gameloop::GameLoop::Instance().GameStartTime();
            double ms = 0.0;
            if (anchor) {
                auto epochMs = std::chrono::duration_cast<std::chrono::milliseconds>(anchor->time_since_epoch());
                ms = static_cast<double>(epochMs.count());
            }
            info.GetReturnValue().Set(ms);
        });

    /// @description Whether the current character is an Expansion (LoD) character.
    /// @type {GameType}
    JSUnit::InstanceProperty(
        context, *me, "gametype", +[](const ub::Local<ub::Name>&, const ub::PropertyCallbackInfo& info) {
            info.GetReturnValue().Set(game::GetGameType());
        });

    /// @description Whether an item is currently held on the cursor (picked up, awaiting placement).
    /// @type {boolean}
    JSUnit::InstanceProperty(
        context, *me, "itemoncursor", +[](const ub::Local<ub::Name>&, const ub::PropertyCallbackInfo& info) {
            info.GetReturnValue().Set(game::Unit::CursorItem().has_value());
        });

    /// @description Ladder status flag of the current realm/game; undefined when the status is unknown (out of game).
    /// @type {number}
    JSUnit::InstanceProperty(
        context, *me, "ladder", +[](const ub::Local<ub::Name>&, const ub::PropertyCallbackInfo& info) {
            if (const auto ladder = game::IsLadder())
                info.GetReturnValue().Set(static_cast<double>(ladder.value()));
        });

    /// @description Current network latency to the game server, in milliseconds.
    /// @type {number}
    JSUnit::InstanceProperty(
        context, *me, "ping", +[](const ub::Local<ub::Name>&, const ub::PropertyCallbackInfo& info) {
            info.GetReturnValue().Set(game::GetPing());
        });

    /// @description Current game render rate, in frames per second.
    /// @type {number}
    JSUnit::InstanceProperty(
        context, *me, "fps", +[](const ub::Local<ub::Name>&, const ub::PropertyCallbackInfo& info) {
            info.GetReturnValue().Set(game::GetFPS());
        });

    /// @description Game client locale/language identifier code.
    /// @type {number}
    JSUnit::InstanceProperty(
        context, *me, "locale", +[](const ub::Local<ub::Name>&, const ub::PropertyCallbackInfo& info) {
            info.GetReturnValue().Set(game::GetLocale());
        });

    /// @description Whether the current character is Hardcore (derived from the hardcore character flag).
    /// @type {boolean}
    JSUnit::InstanceProperty(
        context, *me, "playertype", +[](const ub::Local<ub::Name>&, const ub::PropertyCallbackInfo& info) {
            info.GetReturnValue().Set((game::GetCharFlags() & std::to_underlying(game::CharFlag::Hardcore)) != 0);
        });

    /// @description Full name of the Battle.net realm for the current session. Empty string when not on a realm.
    /// @type {string}
    JSUnit::InstanceProperty(
        context, *me, "realm", +[](const ub::Local<ub::Name>&, const ub::PropertyCallbackInfo& info) {
            std::ignore = info.GetReturnValue().Set(game::GetRealmName());
        });

    /// @description Short/abbreviated name of the Battle.net realm for the current session. Empty string when not on a
    /// realm.
    /// @type {string}
    JSUnit::InstanceProperty(
        context, *me, "realmshort", +[](const ub::Local<ub::Name>&, const ub::PropertyCallbackInfo& info) {
            std::ignore = info.GetReturnValue().Set(game::GetRealmShort());
        });

    /// @description Gold cost to revive the player's current mercenary. 0 when there is no dead merc to revive.
    /// @type {number}
    JSUnit::InstanceProperty(
        context, *me, "mercrevivecost", +[](const ub::Local<ub::Name>&, const ub::PropertyCallbackInfo& info) {
            info.GetReturnValue().Set(game::GetMercReviveCost());
        });

    /// @description Whether the game is fully loaded and ready for interaction (in game, player unit and world data
    /// available).
    /// @type {boolean}
    JSUnit::InstanceProperty(
        context, *me, "gameReady", +[](const ub::Local<ub::Name>&, const ub::PropertyCallbackInfo& info) {
            info.GetReturnValue().Set(game::IsGameReady());
        });

    /// @description Name of the active d2bs profile (from app config) driving this client instance.
    /// @type {string}
    JSUnit::InstanceProperty(
        context, *me, "profile", +[](const ub::Local<ub::Name>&, const ub::PropertyCallbackInfo& info) {
            auto name = config::GetAppConfig().GetProfileName();
            std::ignore = info.GetReturnValue().Set(name);
        });

    /// @description OS process ID of the current game client (GetCurrentProcessId).
    /// @type {number}
    JSUnit::InstanceProperty(
        context, *me, "pid", +[](const ub::Local<ub::Name>&, const ub::PropertyCallbackInfo& info) {
            info.GetReturnValue().Set(static_cast<double>(GetCurrentProcessId()));
        });

    /// @description Whether the "enable unsupported" config flag is set, allowing use of less supported features.
    /// @type {boolean}
    JSUnit::InstanceProperty(
        context, *me, "unsupported", +[](const ub::Local<ub::Name>&, const ub::PropertyCallbackInfo& info) {
            info.GetReturnValue().Set(config::GetAppConfig().enableUnsupported.load());
        });

    /// @description Raw character flags bitfield for the current character.
    /// @type {CharFlag}
    JSUnit::InstanceProperty(
        context, *me, "charflags", +[](const ub::Local<ub::Name>&, const ub::PropertyCallbackInfo& info) {
            info.GetReturnValue().Set(game::GetCharFlags());
        });

    /// @description Screen resolution mode.
    /// @type {ScreenSize}
    JSUnit::InstanceProperty(
        context, *me, "screensize", +[](const ub::Local<ub::Name>&, const ub::PropertyCallbackInfo& info) {
            // Resolution mode (0 = 640x480, 1 = 800x600), derived from the pixel
            // viewport size - the single screen-size source of truth.
            info.GetReturnValue().Set(game::GetViewportSize().width <= 640 ? 0U : 1U);
        });

    /// @description Title text of the game's top-level window.
    /// @type {string}
    JSUnit::InstanceProperty(
        context, *me, "windowtitle", +[](const ub::Local<ub::Name>&, const ub::PropertyCallbackInfo& info) {
            std::ignore = info.GetReturnValue().Set(game::GetWindowTitle());
        });

    /// @description Whether the client is currently in a game (as opposed to in menus / out of game). Less strict than
    /// gameReady.
    /// @type {boolean}
    JSUnit::InstanceProperty(
        context, *me, "ingame", +[](const ub::Local<ub::Name>&, const ub::PropertyCallbackInfo& info) {
            info.GetReturnValue().Set(game::IsInGame());
        });

    /// @description Map generation seed for the current game, which determines the random map layout.
    /// @type {number}
    JSUnit::InstanceProperty(
        context, *me, "mapid", +[](const ub::Local<ub::Name>&, const ub::PropertyCallbackInfo& info) {
            info.GetReturnValue().Set(game::GetMapSeed());
        });

    /// @description Active weapon set.
    /// @type {WeaponSet}
    JSUnit::InstanceProperty(
        context, *me, "weaponswitch", +[](const ub::Local<ub::Name>&, const ub::PropertyCallbackInfo& info) {
            info.GetReturnValue().Set(game::GetWeaponSwitch());
        });

    /// @description Whether the in-game automap overlay is currently displayed; assign to show or hide it.
    /// @type {boolean}
    JSUnit::InstanceProperty(
        context, *me, "automap",
        +[](const ub::Local<ub::Name>&, const ub::PropertyCallbackInfo& info) {
            info.GetReturnValue().Set(game::GetAutomapOn());
        },
        +[](const ub::Local<ub::Name>&, const ub::Local<ub::Value>& value, const ub::PropertyCallbackInfo& info) {
            game::SetAutomapOn(convert::ToBool(info.GetContext(), value));
        });

    /// @description In-game movement mode; assign to switch between walk and run.
    /// @type {MoveMode}
    JSUnit::InstanceProperty(
        context, *me, "runwalk",
        +[](const ub::Local<ub::Name>&, const ub::PropertyCallbackInfo& info) {
            info.GetReturnValue().Set(game::GetAlwaysRun() ? 1U : 0U);
        },
        +[](const ub::Local<ub::Name>&, const ub::Local<ub::Value>& value, const ub::PropertyCallbackInfo& info) {
            game::SetAlwaysRun(convert::ToInt32(info.GetContext(), value) != 0);
        });

    /// @description Bot "chicken" HP threshold (config-backed) at/below which the bot bails out of a game.
    /// @type {number}
    JSUnit::InstanceProperty(
        context, *me, "chickenhp",
        +[](const ub::Local<ub::Name>&, const ub::PropertyCallbackInfo& info) {
            info.GetReturnValue().Set(config::GetAppConfig().chickenHp.load());
        },
        +[](const ub::Local<ub::Name>&, const ub::Local<ub::Value>& value, const ub::PropertyCallbackInfo& info) {
            config::GetAppConfig().chickenHp.store(convert::ToInt32(info.GetContext(), value));
        });

    /// @description Bot "chicken" MP threshold (config-backed) at/below which the bot bails out of a game.
    /// @type {number}
    JSUnit::InstanceProperty(
        context, *me, "chickenmp",
        +[](const ub::Local<ub::Name>&, const ub::PropertyCallbackInfo& info) {
            info.GetReturnValue().Set(config::GetAppConfig().chickenMp.load());
        },
        +[](const ub::Local<ub::Name>&, const ub::Local<ub::Value>& value, const ub::PropertyCallbackInfo& info) {
            config::GetAppConfig().chickenMp.store(convert::ToInt32(info.GetContext(), value));
        });

    /// @description Config flag: whether the bot should quit the game when another player goes hostile.
    /// @type {boolean}
    JSUnit::InstanceProperty(
        context, *me, "quitonhostile",
        +[](const ub::Local<ub::Name>&, const ub::PropertyCallbackInfo& info) {
            info.GetReturnValue().Set(config::GetAppConfig().quitOnHostile.load());
        },
        +[](const ub::Local<ub::Name>&, const ub::Local<ub::Value>& value, const ub::PropertyCallbackInfo& info) {
            config::GetAppConfig().quitOnHostile.store(convert::ToBool(info.GetContext(), value));
        });

    /// @description Config flag: whether keyboard input to the game is blocked/suppressed.
    /// @type {boolean}
    JSUnit::InstanceProperty(
        context, *me, "blockKeys",
        +[](const ub::Local<ub::Name>&, const ub::PropertyCallbackInfo& info) {
            info.GetReturnValue().Set(config::GetAppConfig().blockKeys.load());
        },
        +[](const ub::Local<ub::Name>&, const ub::Local<ub::Value>& value, const ub::PropertyCallbackInfo& info) {
            config::GetAppConfig().blockKeys.store(convert::ToBool(info.GetContext(), value));
        });

    /// @description Config flag: whether mouse input to the game is blocked/suppressed.
    /// @type {boolean}
    JSUnit::InstanceProperty(
        context, *me, "blockMouse",
        +[](const ub::Local<ub::Name>&, const ub::PropertyCallbackInfo& info) {
            info.GetReturnValue().Set(config::GetAppConfig().blockMouse.load());
        },
        +[](const ub::Local<ub::Name>&, const ub::Local<ub::Value>& value, const ub::PropertyCallbackInfo& info) {
            config::GetAppConfig().blockMouse.store(convert::ToBool(info.GetContext(), value));
        });

    /// @description In-game no-pickup state: when on, the character does not auto-pick up items; undefined when out of
    /// game.
    /// @type {boolean}
    JSUnit::InstanceProperty(
        context, *me, "nopickup",
        +[](const ub::Local<ub::Name>&, const ub::PropertyCallbackInfo& info) {
            if (game::GetGameState() != game::GameState::InGame) {
                return;
            }
            info.GetReturnValue().Set(game::GetNoPickUp());
        },
        +[](const ub::Local<ub::Name>&, const ub::Local<ub::Value>& value, const ub::PropertyCallbackInfo& info) {
            if (game::GetGameState() != game::GameState::InGame) {
                return;
            }
            game::SetNoPickUp(convert::ToBool(info.GetContext(), value));
        });

    /// @description Config flag: whether the bot should quit the game when a script error occurs.
    /// @type {boolean}
    JSUnit::InstanceProperty(
        context, *me, "quitonerror",
        +[](const ub::Local<ub::Name>&, const ub::PropertyCallbackInfo& info) {
            info.GetReturnValue().Set(config::GetAppConfig().quitOnError.load());
        },
        +[](const ub::Local<ub::Name>&, const ub::Local<ub::Value>& value, const ub::PropertyCallbackInfo& info) {
            config::GetAppConfig().quitOnError.store(convert::ToBool(info.GetContext(), value));
        });

    /// @description Config-backed maximum game duration in milliseconds before the bot leaves; 0 means no limit.
    /// @type {number}
    JSUnit::InstanceProperty(
        context, *me, "maxgametime",
        +[](const ub::Local<ub::Name>&, const ub::PropertyCallbackInfo& info) {
            info.GetReturnValue().Set(static_cast<uint32_t>(config::GetAppConfig().maxGameTime.load().count()));
        },
        +[](const ub::Local<ub::Name>&, const ub::Local<ub::Value>& value, const ub::PropertyCallbackInfo& info) {
            config::GetAppConfig().maxGameTime.store(
                std::chrono::milliseconds{convert::ToUint32(info.GetContext(), value)});
        });

    return me;
}

}  // namespace d2bs::api::classes
