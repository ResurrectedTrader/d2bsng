#include "ClassRegistry.h"

#include <array>
#include <utility>

#include "api/core/V8Convert.h"
#include "components/gameloop/GameLoop.h"
#include "config/AppConfig.h"
#include "game/Constants.h"
#include "game/GameHelpers.h"
#include "game/Unit.h"

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

// Unwrap tells one class's wrappers from another's by the address of a per-class tag whose
// value is seeded from ClassName, so two classes sharing a name (or colliding under the hash)
// could share a tag and let Unwrap hand back a wrongly-typed pointer. This is the one place
// every class is listed, so the invariant is checked here rather than left to review.
constexpr std::array CLASS_NAMES = {
    JSUnit::ClassName,       JSRoom::ClassName,      JSArea::ClassName,          JSExit::ClassName,
    JSPresetUnit::ClassName, JSParty::ClassName,     JSControl::ClassName,       JSFile::ClassName,
    JSFileTools::ClassName,  JSDirectory::ClassName, JSFrame::ClassName,         JSBox::ClassName,
    JSLine::ClassName,       JSText::ClassName,      JSImage::ClassName,         JSScript::ClassName,
    JSSandbox::ClassName,    JSProfile::ClassName,   JSCompatibility::ClassName, JSHttpClient::ClassName,
    JSSocket::ClassName,     JSSQLite::ClassName,    JSDBStatement::ClassName,   JSTxtTables::ClassName,
};

consteval bool ClassTagsAreDistinct() {
    for (size_t i = 0; i < CLASS_NAMES.size(); ++i) {
        for (size_t j = i + 1; j < CLASS_NAMES.size(); ++j) {
            // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index) - consteval loop
            if (detail::Fnv1a(CLASS_NAMES[i]) == detail::Fnv1a(CLASS_NAMES[j])) {
                return false;
            }
        }
    }
    return true;
}

static_assert(ClassTagsAreDistinct(), "two classes share a ClassName hash - Unwrap would confuse them");

}  // namespace

void RegisterAllClasses(v8::Isolate* isolate, v8::Local<v8::ObjectTemplate> global) {
    // Game objects
    global->Set(isolate, "Unit", JSUnit::GetTemplate(isolate));
    global->Set(isolate, "Room", JSRoom::GetTemplate(isolate));
    global->Set(isolate, "Area", JSArea::GetTemplate(isolate));
    global->Set(isolate, "Exit", JSExit::GetTemplate(isolate));
    global->Set(isolate, "PresetUnit", JSPresetUnit::GetTemplate(isolate));
    global->Set(isolate, "Party", JSParty::GetTemplate(isolate));
    global->Set(isolate, "Control", JSControl::GetTemplate(isolate));

    // File I/O
    global->Set(isolate, "File", JSFile::GetTemplate(isolate));
    global->Set(isolate, "FileTools", JSFileTools::GetTemplate(isolate));
    global->Set(isolate, "Folder", JSDirectory::GetTemplate(isolate));

    // Drawing
    global->Set(isolate, "Frame", JSFrame::GetTemplate(isolate));
    global->Set(isolate, "Box", JSBox::GetTemplate(isolate));
    global->Set(isolate, "Line", JSLine::GetTemplate(isolate));
    global->Set(isolate, "Text", JSText::GetTemplate(isolate));
    global->Set(isolate, "Image", JSImage::GetTemplate(isolate));

    // Script management
    global->Set(isolate, "D2BSScript", JSScript::GetTemplate(isolate));
    global->Set(isolate, "Sandbox", JSSandbox::GetTemplate(isolate));
    global->Set(isolate, "Profile", JSProfile::GetTemplate(isolate));
    global->Set(isolate, "Compatibility", JSCompatibility::GetTemplate(isolate));

    // Network/DB
    global->Set(isolate, "HttpClient", JSHttpClient::GetTemplate(isolate));
    global->Set(isolate, "Socket", JSSocket::GetTemplate(isolate));
    global->Set(isolate, "SQLite", JSSQLite::GetTemplate(isolate));
    global->Set(isolate, "DBStatement", JSDBStatement::GetTemplate(isolate));

    // Game data tables (static namespace; not constructable)
    global->Set(isolate, "TxtTables", JSTxtTables::GetTemplate(isolate));
}

void ClearAllClassCaches(v8::Isolate* isolate) {
    // Game objects
    JSUnit::ClearCache(isolate);
    JSRoom::ClearCache(isolate);
    JSArea::ClearCache(isolate);
    JSExit::ClearCache(isolate);
    JSPresetUnit::ClearCache(isolate);
    JSParty::ClearCache(isolate);
    JSControl::ClearCache(isolate);

    // File I/O
    JSFile::ClearCache(isolate);
    JSFileTools::ClearCache(isolate);
    JSDirectory::ClearCache(isolate);

    // Drawing
    JSFrame::ClearCache(isolate);
    JSBox::ClearCache(isolate);
    JSLine::ClearCache(isolate);
    JSText::ClearCache(isolate);
    JSImage::ClearCache(isolate);

    // Script management
    JSScript::ClearCache(isolate);
    JSSandbox::ClearCache(isolate);
    JSProfile::ClearCache(isolate);
    JSCompatibility::ClearCache(isolate);

    // Network/DB
    JSHttpClient::ClearCache(isolate);
    JSSocket::ClearCache(isolate);
    JSSQLite::ClearCache(isolate);
    JSDBStatement::ClearCache(isolate);

    // Game data tables
    JSTxtTables::ClearCache(isolate);

    v8_convert::ClearKeyCache(isolate);
}

v8::Local<v8::Object> CreateMeObject(v8::Isolate* isolate, v8::Local<v8::Context> context) {
    v8::EscapableHandleScope scope(isolate);

    // 'me': special Unit handle that always resolves to the current player unit.

    // unitId=0, type=0 sentinel: ResolvePtr() returns GetPlayerUnit().
    auto playerHandle = game::Unit::Player();
    auto me = JSUnit::CreateInstance(isolate, context, std::make_unique<game::Unit>(playerHandle));
    if (me.IsEmpty())
        return {};

    /// @description Battle.net / login account name for the current session. Empty string when not logged in.
    /// @type {string}
    JSUnit::InstanceProperty(
        isolate, context, me, "account", +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
            info.GetReturnValue().Set(v8_convert::ToV8(info.GetIsolate(), game::GetAccountName()));
        });

    /// @description The current player character's name. Empty string when out of game.
    /// @type {string}
    JSUnit::InstanceProperty(
        isolate, context, me, "charname", +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
            info.GetReturnValue().Set(v8_convert::ToV8(info.GetIsolate(), game::GetPlayerName()));
        });

    /// @description Current game difficulty.
    /// @type {Difficulty}
    JSUnit::InstanceProperty(
        isolate, context, me, "diff", +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
            info.GetReturnValue().Set(static_cast<uint32_t>(game::GetDifficulty()));
        });

    /// @description Highest difficulty unlocked for this character.
    /// @type {Difficulty}
    JSUnit::InstanceProperty(
        isolate, context, me, "maxdiff", +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
            info.GetReturnValue().Set(static_cast<uint32_t>(game::GetMaxDiff()));
        });

    /// @description Name of the joined/created game. Empty string when not in a game.
    /// @type {string}
    JSUnit::InstanceProperty(
        isolate, context, me, "gamename", +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
            info.GetReturnValue().Set(v8_convert::ToV8(info.GetIsolate(), game::GetGameName()));
        });

    /// @description Password of the joined/created game. Empty string when not in a game or no password set.
    /// @type {string}
    JSUnit::InstanceProperty(
        isolate, context, me, "gamepassword",
        +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
            info.GetReturnValue().Set(v8_convert::ToV8(info.GetIsolate(), game::GetGamePassword()));
        });

    /// @description IP address of the game server for the current session. Empty string when not connected.
    /// @type {string}
    JSUnit::InstanceProperty(
        isolate, context, me, "gameserverip",
        +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
            info.GetReturnValue().Set(v8_convert::ToV8(info.GetIsolate(), game::GetGameServerIp()));
        });

    /// @description Tick (milliseconds, same domain as getTickCount()) marking when the current game started, for
    /// computing elapsed game time. 0 when out of game.
    /// @type {number}
    JSUnit::InstanceProperty(
        isolate, context, me, "gamestarttime",
        +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
            // Game-start anchor as steady_clock epoch ms, matching the
            // getTickCount() domain so script comparisons against it hold;
            // 0 when out of game.
            auto anchor = js::gameloop::GameLoop::Instance().GameStartTime();
            double ms = 0.0;
            if (anchor) {
                auto epochMs = std::chrono::duration_cast<std::chrono::milliseconds>(anchor->time_since_epoch());
                ms = static_cast<double>(epochMs.count());
            }
            info.GetReturnValue().Set(v8_convert::ToV8(info.GetIsolate(), ms));
        });

    /// @description Whether the current character is an Expansion (LoD) character.
    /// @type {GameType}
    JSUnit::InstanceProperty(
        isolate, context, me, "gametype", +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
            info.GetReturnValue().Set(game::GetGameType());
        });

    /// @description Whether an item is currently held on the cursor (picked up, awaiting placement).
    /// @type {boolean}
    JSUnit::InstanceProperty(
        isolate, context, me, "itemoncursor",
        +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
            info.GetReturnValue().Set(game::Unit::CursorItem().has_value());
        });

    /// @description Ladder status flag of the current realm/game; undefined when the status is unknown (out of game).
    /// @type {number}
    JSUnit::InstanceProperty(
        isolate, context, me, "ladder", +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
            if (const auto ladder = game::IsLadder())
                info.GetReturnValue().Set(v8_convert::ToV8(info.GetIsolate(), static_cast<double>(ladder.value())));
        });

    /// @description Current network latency to the game server, in milliseconds.
    /// @type {number}
    JSUnit::InstanceProperty(
        isolate, context, me, "ping", +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
            info.GetReturnValue().Set(game::GetPing());
        });

    /// @description Current game render rate, in frames per second.
    /// @type {number}
    JSUnit::InstanceProperty(
        isolate, context, me, "fps", +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
            info.GetReturnValue().Set(game::GetFPS());
        });

    /// @description Game client locale/language identifier code.
    /// @type {number}
    JSUnit::InstanceProperty(
        isolate, context, me, "locale", +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
            info.GetReturnValue().Set(game::GetLocale());
        });

    /// @description Whether the current character is Hardcore (derived from the hardcore character flag).
    /// @type {boolean}
    JSUnit::InstanceProperty(
        isolate, context, me, "playertype", +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
            info.GetReturnValue().Set((game::GetCharFlags() & std::to_underlying(game::CharFlag::Hardcore)) != 0);
        });

    /// @description Full name of the Battle.net realm for the current session. Empty string when not on a realm.
    /// @type {string}
    JSUnit::InstanceProperty(
        isolate, context, me, "realm", +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
            info.GetReturnValue().Set(v8_convert::ToV8(info.GetIsolate(), game::GetRealmName()));
        });

    /// @description Short/abbreviated name of the Battle.net realm for the current session. Empty string when not on a
    /// realm.
    /// @type {string}
    JSUnit::InstanceProperty(
        isolate, context, me, "realmshort", +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
            info.GetReturnValue().Set(v8_convert::ToV8(info.GetIsolate(), game::GetRealmShort()));
        });

    /// @description Gold cost to revive the player's current mercenary. 0 when there is no dead merc to revive.
    /// @type {number}
    JSUnit::InstanceProperty(
        isolate, context, me, "mercrevivecost",
        +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
            info.GetReturnValue().Set(game::GetMercReviveCost());
        });

    /// @description Whether the game is fully loaded and ready for interaction (in game, player unit and world data
    /// available).
    /// @type {boolean}
    JSUnit::InstanceProperty(
        isolate, context, me, "gameReady", +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
            info.GetReturnValue().Set(game::IsGameReady());
        });

    /// @description Name of the active d2bs profile (from app config) driving this client instance.
    /// @type {string}
    JSUnit::InstanceProperty(
        isolate, context, me, "profile", +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
            auto name = config::GetAppConfig().GetProfileName();
            info.GetReturnValue().Set(v8_convert::ToV8(info.GetIsolate(), name));
        });

    /// @description OS process ID of the current game client (GetCurrentProcessId).
    /// @type {number}
    JSUnit::InstanceProperty(
        isolate, context, me, "pid", +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
            info.GetReturnValue().Set(v8_convert::ToV8(info.GetIsolate(), static_cast<double>(GetCurrentProcessId())));
        });

    /// @description Whether the "enable unsupported" config flag is set, allowing use of less supported features.
    /// @type {boolean}
    JSUnit::InstanceProperty(
        isolate, context, me, "unsupported", +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
            info.GetReturnValue().Set(config::GetAppConfig().enableUnsupported.load());
        });

    /// @description Raw character flags bitfield for the current character.
    /// @type {CharFlag}
    JSUnit::InstanceProperty(
        isolate, context, me, "charflags", +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
            info.GetReturnValue().Set(game::GetCharFlags());
        });

    /// @description Screen resolution mode.
    /// @type {ScreenSize}
    JSUnit::InstanceProperty(
        isolate, context, me, "screensize", +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
            // Resolution mode (0 = 640x480, 1 = 800x600), derived from the pixel
            // viewport size - the single screen-size source of truth.
            info.GetReturnValue().Set(game::GetViewportSize().width <= 640 ? 0U : 1U);
        });

    /// @description Title text of the game's top-level window.
    /// @type {string}
    JSUnit::InstanceProperty(
        isolate, context, me, "windowtitle", +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
            info.GetReturnValue().Set(v8_convert::ToV8(info.GetIsolate(), game::GetWindowTitle()));
        });

    /// @description Whether the client is currently in a game (as opposed to in menus / out of game). Less strict than
    /// gameReady.
    /// @type {boolean}
    JSUnit::InstanceProperty(
        isolate, context, me, "ingame", +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
            info.GetReturnValue().Set(game::IsInGame());
        });

    /// @description Map generation seed for the current game, which determines the random map layout.
    /// @type {number}
    JSUnit::InstanceProperty(
        isolate, context, me, "mapid", +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
            info.GetReturnValue().Set(game::GetMapSeed());
        });

    /// @description Active weapon set.
    /// @type {WeaponSet}
    JSUnit::InstanceProperty(
        isolate, context, me, "weaponswitch",
        +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
            info.GetReturnValue().Set(game::GetWeaponSwitch());
        });

    /// @description Whether the in-game automap overlay is currently displayed; assign to show or hide it.
    /// @type {boolean}
    JSUnit::InstanceProperty(
        isolate, context, me, "automap",
        +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
            info.GetReturnValue().Set(game::GetAutomapOn());
        },
        +[](v8::Local<v8::Name>, v8::Local<v8::Value> value, const v8::PropertyCallbackInfo<void>& info) {
            game::SetAutomapOn(value->BooleanValue(info.GetIsolate()));
        });

    /// @description In-game movement mode; assign to switch between walk and run.
    /// @type {MoveMode}
    JSUnit::InstanceProperty(
        isolate, context, me, "runwalk",
        +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
            info.GetReturnValue().Set(game::GetAlwaysRun() ? 1U : 0U);
        },
        +[](v8::Local<v8::Name>, v8::Local<v8::Value> value, const v8::PropertyCallbackInfo<void>& info) {
            game::SetAlwaysRun(v8_convert::ToInt32(info.GetIsolate(), value) != 0);
        });

    /// @description Bot "chicken" HP threshold (config-backed) at/below which the bot bails out of a game.
    /// @type {number}
    JSUnit::InstanceProperty(
        isolate, context, me, "chickenhp",
        +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
            info.GetReturnValue().Set(config::GetAppConfig().chickenHp.load());
        },
        +[](v8::Local<v8::Name>, v8::Local<v8::Value> value, const v8::PropertyCallbackInfo<void>& info) {
            config::GetAppConfig().chickenHp.store(v8_convert::ToInt32(info.GetIsolate(), value));
        });

    /// @description Bot "chicken" MP threshold (config-backed) at/below which the bot bails out of a game.
    /// @type {number}
    JSUnit::InstanceProperty(
        isolate, context, me, "chickenmp",
        +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
            info.GetReturnValue().Set(config::GetAppConfig().chickenMp.load());
        },
        +[](v8::Local<v8::Name>, v8::Local<v8::Value> value, const v8::PropertyCallbackInfo<void>& info) {
            config::GetAppConfig().chickenMp.store(v8_convert::ToInt32(info.GetIsolate(), value));
        });

    /// @description Config flag: whether the bot should quit the game when another player goes hostile.
    /// @type {boolean}
    JSUnit::InstanceProperty(
        isolate, context, me, "quitonhostile",
        +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
            info.GetReturnValue().Set(config::GetAppConfig().quitOnHostile.load());
        },
        +[](v8::Local<v8::Name>, v8::Local<v8::Value> value, const v8::PropertyCallbackInfo<void>& info) {
            config::GetAppConfig().quitOnHostile.store(value->BooleanValue(info.GetIsolate()));
        });

    /// @description Config flag: whether keyboard input to the game is blocked/suppressed.
    /// @type {boolean}
    JSUnit::InstanceProperty(
        isolate, context, me, "blockKeys",
        +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
            info.GetReturnValue().Set(config::GetAppConfig().blockKeys.load());
        },
        +[](v8::Local<v8::Name>, v8::Local<v8::Value> value, const v8::PropertyCallbackInfo<void>& info) {
            config::GetAppConfig().blockKeys.store(value->BooleanValue(info.GetIsolate()));
        });

    /// @description Config flag: whether mouse input to the game is blocked/suppressed.
    /// @type {boolean}
    JSUnit::InstanceProperty(
        isolate, context, me, "blockMouse",
        +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
            info.GetReturnValue().Set(config::GetAppConfig().blockMouse.load());
        },
        +[](v8::Local<v8::Name>, v8::Local<v8::Value> value, const v8::PropertyCallbackInfo<void>& info) {
            config::GetAppConfig().blockMouse.store(value->BooleanValue(info.GetIsolate()));
        });

    /// @description In-game no-pickup state: when on, the character does not auto-pick up items; undefined when out of
    /// game.
    /// @type {boolean}
    JSUnit::InstanceProperty(
        isolate, context, me, "nopickup",
        +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
            if (game::GetGameState() != game::GameState::InGame) {
                return;
            }
            info.GetReturnValue().Set(game::GetNoPickUp());
        },
        +[](v8::Local<v8::Name>, v8::Local<v8::Value> value, const v8::PropertyCallbackInfo<void>& info) {
            if (game::GetGameState() != game::GameState::InGame) {
                return;
            }
            game::SetNoPickUp(v8_convert::ToBool(info.GetIsolate(), value));
        });

    /// @description Config flag: whether the bot should quit the game when a script error occurs.
    /// @type {boolean}
    JSUnit::InstanceProperty(
        isolate, context, me, "quitonerror",
        +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
            info.GetReturnValue().Set(config::GetAppConfig().quitOnError.load());
        },
        +[](v8::Local<v8::Name>, v8::Local<v8::Value> value, const v8::PropertyCallbackInfo<void>& info) {
            config::GetAppConfig().quitOnError.store(value->BooleanValue(info.GetIsolate()));
        });

    /// @description Config-backed maximum game duration in milliseconds before the bot leaves; 0 means no limit.
    /// @type {number}
    JSUnit::InstanceProperty(
        isolate, context, me, "maxgametime",
        +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
            info.GetReturnValue().Set(static_cast<uint32_t>(config::GetAppConfig().maxGameTime.load().count()));
        },
        +[](v8::Local<v8::Name>, v8::Local<v8::Value> value, const v8::PropertyCallbackInfo<void>& info) {
            config::GetAppConfig().maxGameTime.store(
                std::chrono::milliseconds{v8_convert::ToUint32(info.GetIsolate(), value)});
        });

    return scope.Escape(me);
}

}  // namespace d2bs::api::classes
