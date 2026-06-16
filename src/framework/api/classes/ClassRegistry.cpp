#include "ClassRegistry.h"
#include "api/core/V8Convert.h"
#include "components/config/AppConfig.h"
#include "components/gameloop/GameLoop.h"
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
#include "game/JSUnit.h"
#include "io/JSDBStatement.h"
#include "io/JSDirectory.h"
#include "io/JSFile.h"
#include "io/JSFileTools.h"
#include "io/JSHttpClient.h"
#include "io/JSSQLite.h"
#include "io/JSSocket.h"
#include "scripting/JSProfile.h"
#include "scripting/JSSandbox.h"
#include "scripting/JSScript.h"

namespace d2bs::api::classes {

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

    // Network/DB
    global->Set(isolate, "HttpClient", JSHttpClient::GetTemplate(isolate));
    global->Set(isolate, "Socket", JSSocket::GetTemplate(isolate));
    global->Set(isolate, "SQLite", JSSQLite::GetTemplate(isolate));
    global->Set(isolate, "DBStatement", JSDBStatement::GetTemplate(isolate));
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

    // Network/DB
    JSHttpClient::ClearCache(isolate);
    JSSocket::ClearCache(isolate);
    JSSQLite::ClearCache(isolate);
    JSDBStatement::ClearCache(isolate);
}

v8::Local<v8::Object> CreateMeObject(v8::Isolate* isolate, v8::Local<v8::Context> context) {
    v8::EscapableHandleScope scope(isolate);

    // 'me': special Unit handle that always resolves to the current player unit.

    // unitId=0, type=0 sentinel: ResolvePtr() returns GetPlayerUnit().
    auto playerHandle = d2bs::game::Unit::Player();
    auto me = JSUnit::CreateInstance(isolate, context, std::make_unique<d2bs::game::Unit>(playerHandle));
    if (me.IsEmpty())
        return {};

    /// @description Battle.net / login account name for the current session. Empty string when not logged in.
    /// @type {string}
    me->SetNativeDataProperty(
          context, v8_convert::ToV8(isolate, "account"),
          +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
              info.GetReturnValue().Set(v8_convert::ToV8(info.GetIsolate(), d2bs::game::GetAccountName()));
          },
          nullptr, v8::Local<v8::Value>(), v8::PropertyAttribute::ReadOnly)
        .Check();

    /// @description The current player character's name. Empty string when out of game.
    /// @type {string}
    me->SetNativeDataProperty(
          context, v8_convert::ToV8(isolate, "charname"),
          +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
              info.GetReturnValue().Set(v8_convert::ToV8(info.GetIsolate(), d2bs::game::GetPlayerName()));
          },
          nullptr, v8::Local<v8::Value>(), v8::PropertyAttribute::ReadOnly)
        .Check();

    /// @description Current game difficulty index (0 = Normal, 1 = Nightmare, 2 = Hell).
    /// @type {number}
    me->SetNativeDataProperty(
          context, v8_convert::ToV8(isolate, "diff"),
          +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
              info.GetReturnValue().Set(static_cast<uint32_t>(d2bs::game::GetDifficulty()));
          },
          nullptr, v8::Local<v8::Value>(), v8::PropertyAttribute::ReadOnly)
        .Check();

    /// @description Highest difficulty index unlocked for this character (0 = Normal, 1 = Nightmare, 2 = Hell).
    /// @type {number}
    me->SetNativeDataProperty(
          context, v8_convert::ToV8(isolate, "maxdiff"),
          +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
              info.GetReturnValue().Set(static_cast<uint32_t>(d2bs::game::GetMaxDiff()));
          },
          nullptr, v8::Local<v8::Value>(), v8::PropertyAttribute::ReadOnly)
        .Check();

    /// @description Name of the joined/created game. Empty string when not in a game.
    /// @type {string}
    me->SetNativeDataProperty(
          context, v8_convert::ToV8(isolate, "gamename"),
          +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
              info.GetReturnValue().Set(v8_convert::ToV8(info.GetIsolate(), d2bs::game::GetGameName()));
          },
          nullptr, v8::Local<v8::Value>(), v8::PropertyAttribute::ReadOnly)
        .Check();

    /// @description Password of the joined/created game. Empty string when not in a game or no password set.
    /// @type {string}
    me->SetNativeDataProperty(
          context, v8_convert::ToV8(isolate, "gamepassword"),
          +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
              info.GetReturnValue().Set(v8_convert::ToV8(info.GetIsolate(), d2bs::game::GetGamePassword()));
          },
          nullptr, v8::Local<v8::Value>(), v8::PropertyAttribute::ReadOnly)
        .Check();

    /// @description IP address of the game server for the current session. Empty string when not connected.
    /// @type {string}
    me->SetNativeDataProperty(
          context, v8_convert::ToV8(isolate, "gameserverip"),
          +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
              info.GetReturnValue().Set(v8_convert::ToV8(info.GetIsolate(), d2bs::game::GetGameServerIp()));
          },
          nullptr, v8::Local<v8::Value>(), v8::PropertyAttribute::ReadOnly)
        .Check();

    /// @description Tick (milliseconds, same domain as getTickCount()) marking when the current game started, for
    /// computing elapsed game time. 0 when out of game.
    /// @type {number}
    me->SetNativeDataProperty(
          context, v8_convert::ToV8(isolate, "gamestarttime"),
          +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
              // Game-start anchor as steady_clock epoch ms, matching the
              // getTickCount() domain so script comparisons against it hold;
              // 0 when out of game.
              auto anchor = d2bs::framework::gameloop::GameLoop::Instance().GameStartTime();
              double ms = 0.0;
              if (anchor) {
                  auto epochMs = std::chrono::duration_cast<std::chrono::milliseconds>(anchor->time_since_epoch());
                  ms = static_cast<double>(epochMs.count());
              }
              info.GetReturnValue().Set(v8_convert::ToV8(info.GetIsolate(), ms));
          },
          nullptr, v8::Local<v8::Value>(), v8::PropertyAttribute::ReadOnly)
        .Check();

    /// @description Whether the current character is an Expansion (LoD) character: 0 = classic, 1 = expansion.
    /// @type {number}
    me->SetNativeDataProperty(
          context, v8_convert::ToV8(isolate, "gametype"),
          +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
              info.GetReturnValue().Set(d2bs::game::GetGameType());
          },
          nullptr, v8::Local<v8::Value>(), v8::PropertyAttribute::ReadOnly)
        .Check();

    /// @description Whether an item is currently held on the cursor (picked up, awaiting placement).
    /// @type {boolean}
    me->SetNativeDataProperty(
          context, v8_convert::ToV8(isolate, "itemoncursor"),
          +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
              info.GetReturnValue().Set(d2bs::game::Unit::CursorItem().has_value());
          },
          nullptr, v8::Local<v8::Value>(), v8::PropertyAttribute::ReadOnly)
        .Check();

    /// @description Ladder status flag of the current realm/game; undefined when the status is unknown (out of game).
    /// @type {number}
    me->SetNativeDataProperty(
          context, v8_convert::ToV8(isolate, "ladder"),
          +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
              if (const auto ladder = d2bs::game::IsLadder())
                  info.GetReturnValue().Set(v8_convert::ToV8(info.GetIsolate(), static_cast<double>(ladder.value())));
          },
          nullptr, v8::Local<v8::Value>(), v8::PropertyAttribute::ReadOnly)
        .Check();

    /// @description Current network latency to the game server, in milliseconds.
    /// @type {number}
    me->SetNativeDataProperty(
          context, v8_convert::ToV8(isolate, "ping"),
          +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
              info.GetReturnValue().Set(d2bs::game::GetPing());
          },
          nullptr, v8::Local<v8::Value>(), v8::PropertyAttribute::ReadOnly)
        .Check();

    /// @description Current game render rate, in frames per second.
    /// @type {number}
    me->SetNativeDataProperty(
          context, v8_convert::ToV8(isolate, "fps"),
          +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
              info.GetReturnValue().Set(d2bs::game::GetFPS());
          },
          nullptr, v8::Local<v8::Value>(), v8::PropertyAttribute::ReadOnly)
        .Check();

    /// @description Game client locale/language identifier code.
    /// @type {number}
    me->SetNativeDataProperty(
          context, v8_convert::ToV8(isolate, "locale"),
          +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
              info.GetReturnValue().Set(d2bs::game::GetLocale());
          },
          nullptr, v8::Local<v8::Value>(), v8::PropertyAttribute::ReadOnly)
        .Check();

    /// @description Whether the current character is Hardcore (derived from the hardcore character flag).
    /// @type {boolean}
    me->SetNativeDataProperty(
          context, v8_convert::ToV8(isolate, "playertype"),
          +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
              info.GetReturnValue().Set((d2bs::game::GetCharFlags() & d2bs::game::CHAR_FLAG_HARDCORE) ==
                                        d2bs::game::CHAR_FLAG_HARDCORE);
          },
          nullptr, v8::Local<v8::Value>(), v8::PropertyAttribute::ReadOnly)
        .Check();

    /// @description Full name of the Battle.net realm for the current session. Empty string when not on a realm.
    /// @type {string}
    me->SetNativeDataProperty(
          context, v8_convert::ToV8(isolate, "realm"),
          +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
              info.GetReturnValue().Set(v8_convert::ToV8(info.GetIsolate(), d2bs::game::GetRealmName()));
          },
          nullptr, v8::Local<v8::Value>(), v8::PropertyAttribute::ReadOnly)
        .Check();

    /// @description Short/abbreviated name of the Battle.net realm for the current session. Empty string when not on a
    /// realm.
    /// @type {string}
    me->SetNativeDataProperty(
          context, v8_convert::ToV8(isolate, "realmshort"),
          +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
              info.GetReturnValue().Set(v8_convert::ToV8(info.GetIsolate(), d2bs::game::GetRealmShort()));
          },
          nullptr, v8::Local<v8::Value>(), v8::PropertyAttribute::ReadOnly)
        .Check();

    /// @description Gold cost to revive the player's current mercenary. 0 when there is no dead merc to revive.
    /// @type {number}
    me->SetNativeDataProperty(
          context, v8_convert::ToV8(isolate, "mercrevivecost"),
          +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
              info.GetReturnValue().Set(d2bs::game::GetMercReviveCost());
          },
          nullptr, v8::Local<v8::Value>(), v8::PropertyAttribute::ReadOnly)
        .Check();

    /// @description Whether the game is fully loaded and ready for interaction (in game, player unit and world data
    /// available).
    /// @type {boolean}
    me->SetNativeDataProperty(
          context, v8_convert::ToV8(isolate, "gameReady"),
          +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
              info.GetReturnValue().Set(d2bs::game::IsGameReady());
          },
          nullptr, v8::Local<v8::Value>(), v8::PropertyAttribute::ReadOnly)
        .Check();

    /// @description Name of the active d2bs profile (from app config) driving this client instance.
    /// @type {string}
    me->SetNativeDataProperty(
          context, v8_convert::ToV8(isolate, "profile"),
          +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
              auto name = d2bs::config::GetAppConfig().GetProfileName();
              info.GetReturnValue().Set(v8_convert::ToV8(info.GetIsolate(), name));
          },
          nullptr, v8::Local<v8::Value>(), v8::PropertyAttribute::ReadOnly)
        .Check();

    /// @description OS process ID of the current game client (GetCurrentProcessId).
    /// @type {number}
    me->SetNativeDataProperty(
          context, v8_convert::ToV8(isolate, "pid"),
          +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
              info.GetReturnValue().Set(
                  v8_convert::ToV8(info.GetIsolate(), static_cast<double>(GetCurrentProcessId())));
          },
          nullptr, v8::Local<v8::Value>(), v8::PropertyAttribute::ReadOnly)
        .Check();

    /// @description Whether the "enable unsupported" config flag is set, allowing use of less supported features.
    /// @type {boolean}
    me->SetNativeDataProperty(
          context, v8_convert::ToV8(isolate, "unsupported"),
          +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
              info.GetReturnValue().Set(d2bs::config::GetAppConfig().enableUnsupported.load());
          },
          nullptr, v8::Local<v8::Value>(), v8::PropertyAttribute::ReadOnly)
        .Check();

    /// @description Raw character flags bitfield for the current character. Bits: 0x04 = hardcore, 0x20 = expansion,
    /// 0x40 = ladder.
    /// @type {number}
    me->SetNativeDataProperty(
          context, v8_convert::ToV8(isolate, "charflags"),
          +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
              info.GetReturnValue().Set(d2bs::game::GetCharFlags());
          },
          nullptr, v8::Local<v8::Value>(), v8::PropertyAttribute::ReadOnly)
        .Check();

    /// @description Screen resolution mode index (e.g. 0 = 640x480, 1 = 800x600).
    /// @type {number}
    me->SetNativeDataProperty(
          context, v8_convert::ToV8(isolate, "screensize"),
          +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
              info.GetReturnValue().Set(d2bs::game::GetScreenSize());
          },
          nullptr, v8::Local<v8::Value>(), v8::PropertyAttribute::ReadOnly)
        .Check();

    /// @description Title text of the game's top-level window.
    /// @type {string}
    me->SetNativeDataProperty(
          context, v8_convert::ToV8(isolate, "windowtitle"),
          +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
              info.GetReturnValue().Set(v8_convert::ToV8(info.GetIsolate(), d2bs::game::GetWindowTitle()));
          },
          nullptr, v8::Local<v8::Value>(), v8::PropertyAttribute::ReadOnly)
        .Check();

    /// @description Whether the client is currently in a game (as opposed to in menus / out of game). Less strict than
    /// gameReady.
    /// @type {boolean}
    me->SetNativeDataProperty(
          context, v8_convert::ToV8(isolate, "ingame"),
          +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
              info.GetReturnValue().Set(d2bs::game::IsInGame());
          },
          nullptr, v8::Local<v8::Value>(), v8::PropertyAttribute::ReadOnly)
        .Check();

    /// @description Map generation seed for the current game, which determines the random map layout.
    /// @type {number}
    me->SetNativeDataProperty(
          context, v8_convert::ToV8(isolate, "mapid"),
          +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
              info.GetReturnValue().Set(d2bs::game::GetMapSeed());
          },
          nullptr, v8::Local<v8::Value>(), v8::PropertyAttribute::ReadOnly)
        .Check();

    /// @description Active weapon set index (0 = primary slot I, 1 = secondary slot II).
    /// @type {number}
    me->SetNativeDataProperty(
          context, v8_convert::ToV8(isolate, "weaponswitch"),
          +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
              info.GetReturnValue().Set(d2bs::game::GetWeaponSwitch());
          },
          nullptr, v8::Local<v8::Value>(), v8::PropertyAttribute::ReadOnly)
        .Check();

    /// @description Whether the in-game automap overlay is currently displayed; assign to show or hide it.
    /// @type {boolean}
    me->SetNativeDataProperty(
          context, v8_convert::ToV8(isolate, "automap"),
          +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
              info.GetReturnValue().Set(d2bs::game::GetAutomapOn());
          },
          +[](v8::Local<v8::Name>, v8::Local<v8::Value> value, const v8::PropertyCallbackInfo<void>& info) {
              d2bs::game::SetAutomapOn(value->BooleanValue(info.GetIsolate()));
          })
        .Check();

    /// @description In-game movement mode: true = always run, false = walk.
    /// @type {boolean}
    me->SetNativeDataProperty(
          context, v8_convert::ToV8(isolate, "runwalk"),
          +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
              info.GetReturnValue().Set(d2bs::game::GetAlwaysRun());
          },
          +[](v8::Local<v8::Name>, v8::Local<v8::Value> value, const v8::PropertyCallbackInfo<void>& info) {
              d2bs::game::SetAlwaysRun(v8_convert::ToBool(info.GetIsolate(), value));
          })
        .Check();

    /// @description Bot "chicken" HP threshold (config-backed) at/below which the bot bails out of a game.
    /// @type {number}
    me->SetNativeDataProperty(
          context, v8_convert::ToV8(isolate, "chickenhp"),
          +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
              info.GetReturnValue().Set(d2bs::config::GetAppConfig().chickenHp.load());
          },
          +[](v8::Local<v8::Name>, v8::Local<v8::Value> value, const v8::PropertyCallbackInfo<void>& info) {
              d2bs::config::GetAppConfig().chickenHp.store(v8_convert::ToInt32(info.GetIsolate(), value));
          })
        .Check();

    /// @description Bot "chicken" MP threshold (config-backed) at/below which the bot bails out of a game.
    /// @type {number}
    me->SetNativeDataProperty(
          context, v8_convert::ToV8(isolate, "chickenmp"),
          +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
              info.GetReturnValue().Set(d2bs::config::GetAppConfig().chickenMp.load());
          },
          +[](v8::Local<v8::Name>, v8::Local<v8::Value> value, const v8::PropertyCallbackInfo<void>& info) {
              d2bs::config::GetAppConfig().chickenMp.store(v8_convert::ToInt32(info.GetIsolate(), value));
          })
        .Check();

    /// @description Config flag: whether the bot should quit the game when another player goes hostile.
    /// @type {boolean}
    me->SetNativeDataProperty(
          context, v8_convert::ToV8(isolate, "quitonhostile"),
          +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
              info.GetReturnValue().Set(d2bs::config::GetAppConfig().quitOnHostile.load());
          },
          +[](v8::Local<v8::Name>, v8::Local<v8::Value> value, const v8::PropertyCallbackInfo<void>& info) {
              d2bs::config::GetAppConfig().quitOnHostile.store(value->BooleanValue(info.GetIsolate()));
          })
        .Check();

    /// @description Config flag: whether keyboard input to the game is blocked/suppressed.
    /// @type {boolean}
    me->SetNativeDataProperty(
          context, v8_convert::ToV8(isolate, "blockKeys"),
          +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
              info.GetReturnValue().Set(d2bs::config::GetAppConfig().blockKeys.load());
          },
          +[](v8::Local<v8::Name>, v8::Local<v8::Value> value, const v8::PropertyCallbackInfo<void>& info) {
              d2bs::config::GetAppConfig().blockKeys.store(value->BooleanValue(info.GetIsolate()));
          })
        .Check();

    /// @description Config flag: whether mouse input to the game is blocked/suppressed.
    /// @type {boolean}
    me->SetNativeDataProperty(
          context, v8_convert::ToV8(isolate, "blockMouse"),
          +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
              info.GetReturnValue().Set(d2bs::config::GetAppConfig().blockMouse.load());
          },
          +[](v8::Local<v8::Name>, v8::Local<v8::Value> value, const v8::PropertyCallbackInfo<void>& info) {
              d2bs::config::GetAppConfig().blockMouse.store(value->BooleanValue(info.GetIsolate()));
          })
        .Check();

    /// @description In-game no-pickup state: when on, the character does not auto-pick up items; undefined when out of
    /// game.
    /// @type {boolean}
    me->SetNativeDataProperty(
          context, v8_convert::ToV8(isolate, "nopickup"),
          +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
              if (d2bs::game::GetGameState() != d2bs::game::GameState::InGame) {
                  return;
              }
              info.GetReturnValue().Set(d2bs::game::GetNoPickUp());
          },
          +[](v8::Local<v8::Name>, v8::Local<v8::Value> value, const v8::PropertyCallbackInfo<void>& info) {
              if (d2bs::game::GetGameState() != d2bs::game::GameState::InGame) {
                  return;
              }
              d2bs::game::SetNoPickUp(v8_convert::ToBool(info.GetIsolate(), value));
          })
        .Check();

    /// @description Config flag: whether the bot should quit the game when a script error occurs.
    /// @type {boolean}
    me->SetNativeDataProperty(
          context, v8_convert::ToV8(isolate, "quitonerror"),
          +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
              info.GetReturnValue().Set(d2bs::config::GetAppConfig().quitOnError.load());
          },
          +[](v8::Local<v8::Name>, v8::Local<v8::Value> value, const v8::PropertyCallbackInfo<void>& info) {
              d2bs::config::GetAppConfig().quitOnError.store(value->BooleanValue(info.GetIsolate()));
          })
        .Check();

    /// @description Config-backed maximum game duration in milliseconds before the bot leaves; 0 means no limit.
    /// @type {number}
    me->SetNativeDataProperty(
          context, v8_convert::ToV8(isolate, "maxgametime"),
          +[](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info) {
              info.GetReturnValue().Set(static_cast<uint32_t>(d2bs::config::GetAppConfig().maxGameTime.load().count()));
          },
          +[](v8::Local<v8::Name>, v8::Local<v8::Value> value, const v8::PropertyCallbackInfo<void>& info) {
              d2bs::config::GetAppConfig().maxGameTime.store(
                  std::chrono::milliseconds{v8_convert::ToUint32(info.GetIsolate(), value)});
          })
        .Check();

    return scope.Escape(me);
}

}  // namespace d2bs::api::classes
