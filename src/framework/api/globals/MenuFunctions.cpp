#include "MenuFunctions.h"

#include <cstdint>

#include "api/core/V8Convert.h"
#include "api/core/V8Error.h"
#include "api/core/V8Function.h"
#include "components/config/AppConfig.h"
#include "components/profile/ProfileData.h"
#include "components/profile/ProfileService.h"
#include "game/GameHelpers.h"
#include "game/Menu.h"

namespace d2bs::api::globals {

void RegisterMenuFunctions(v8::Isolate* isolate, v8::Local<v8::ObjectTemplate> global) {
    /// @description Logs in at the menu using a stored profile, driving the UI through character selection.
    /// @signature login()
    /// @signature login(profileName: string)
    /// @param profileName {string} - Name of the stored profile; defaults to the configured app profile.
    /// @returns {undefined} - No value; no-op unless at the menu. Throws on missing/invalid profile or login failure.
    /// @throws {Error} - Profile name resolves empty (none passed, none configured).
    /// @throws {Error} - Named profile does not exist.
    /// @throws {Error} - Login attempt fails.
    v8_function::Register(
        isolate, global, "login", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
            auto* isolate = args.GetIsolate();

            // Reference: only works when client is in menu state
            if (game::GetGameState() != game::GameState::Menu) {
                return;
            }

            std::string profileName;
            if (args.Length() > 0 && args[0]->IsString()) {
                profileName = v8_convert::ToString(isolate, args[0]);
            } else {
                profileName = config::GetAppConfig().GetProfileName();
                if (profileName.empty()) {
                    v8_error::ThrowError(isolate, "Invalid profile specified!");
                    return;
                }
            }

            auto profile = d2bs::profile::Load(profileName);
            if (!profile) {
                v8_error::ThrowError(isolate, "Profile does not exist!");
                return;
            }

            // Set name only on success: reference sets it before ProfileExists, leaving szProfile
            // pointing at a missing profile after a failed login. We defer to avoid that.
            config::GetAppConfig().SetProfileName(profileName);

            auto result = game::Login(*profile);
            if (result.status != game::LoginStatus::Success) {
                v8_error::ThrowError(isolate, result.errorMessage);
            }
        });

    /// @description Selects a profile's character in the character-selection screen.
    /// @signature selectCharacter(profileName: string)
    /// @param profileName {string} - Name of the stored profile whose character should be selected.
    /// @returns {boolean} - true if the character was selected, false otherwise.
    /// @throws {Error} - Profile cannot be resolved to a character.
    v8_function::Register(
        isolate, global, "selectCharacter", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
            auto* isolate = args.GetIsolate();

            // Reference JSMenu.cpp:45-46 throws one message for both wrong arg
            // count and wrong arg type; reproduce verbatim so scripts that
            // string-match the error keep working.
            if (args.Length() != 1 || !args[0]->IsString()) {
                v8_error::ThrowError(isolate, "Invalid parameters specified to selectCharacter");
                return;
            }

            std::string profileName = v8_convert::ToString(isolate, args[0]);
            auto charname = d2bs::profile::ResolveCharacter(profileName);
            if (!charname) {
                v8_error::ThrowError(isolate, "Invalid profile specified");
                return;
            }
            args.GetReturnValue().Set(game::SelectCharacter(*charname));
        });

    /// @description Creates a new character at the menu with the given name and class.
    /// @signature createCharacter(name: string, type: number, hardcore?: boolean, ladder?: boolean)
    /// @param name {string} - Desired character name.
    /// @param type {number} - Character class index: 0 = Amazon, 1 = Sorceress, 2 = Necromancer, 3 = Paladin, 4 =
    /// Barbarian, 5 = Druid, 6 = Assassin.
    /// @param hardcore {boolean} - Create as hardcore; defaults to false.
    /// @param ladder {boolean} - Create as ladder; defaults to false.
    /// @returns {boolean} - Result of the creation attempt; undefined if not at the menu. Throws on invalid arguments.
    /// @throws {Error} - Character type is outside the valid class range (0-6).
    v8_function::Register(
        isolate, global, "createCharacter", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
            auto* isolate = args.GetIsolate();

            // Reference: only works when client is in menu state
            if (game::GetGameState() != game::GameState::Menu) {
                return;
            }

            if (args.Length() < 2) {
                v8_error::ThrowError(isolate, "createCharacter requires at least 2 arguments");
                return;
            }

            if (!v8_error::CheckIsString(args, 0, "name")) {
                return;
            }

            if (!v8_error::CheckIsNumber(args, 1, "type")) {
                return;
            }

            std::string name = v8_convert::ToString(isolate, args[0]);
            int32_t type = v8_convert::ToInt32(isolate, args[1]);

            bool hardcore = false;
            if (args.Length() > 2 && args[2]->IsBoolean()) {
                hardcore = args[2]->BooleanValue(isolate);
            }

            bool ladder = false;
            if (args.Length() > 3 && args[3]->IsBoolean()) {
                ladder = args[3]->BooleanValue(isolate);
            }

            // Validate character class (0-6)
            if (type < 0 || type > static_cast<int32_t>(game::CharacterClass::Assassin)) {
                v8_error::ThrowError(isolate, "Invalid character type");
                return;
            }

            args.GetReturnValue().Set(
                game::CreateCharacter(name, static_cast<game::CharacterClass>(type), hardcore, ladder));
        });

    /// @description Creates an online game at the menu with the given name, password, and difficulty.
    /// @signature createGame(name: string, password?: string, difficulty?: number)
    /// @param name {string} - Game name, max 15 characters.
    /// @param password {string} - Game password, max 15 characters.
    /// @param difficulty {number} - Difficulty: 0 = Normal, 1 = Nightmare, 2 = Hell, 3 = highest available; defaults to
    /// 3.
    /// @returns {null} - Always null; no-op unless at the menu. Throws on validation or create failure.
    /// @throws {Error} - Game name or password exceeds 15 characters.
    /// @throws {Error} - Difficulty is outside 0-3.
    /// @throws {Error} - Create attempt fails.
    v8_function::Register(
        isolate, global, "createGame", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
            auto* isolate = args.GetIsolate();
            args.GetReturnValue().SetNull();

            // Reference: only works when client is in menu state
            if (game::GetGameState() != game::GameState::Menu) {
                return;
            }

            if (!v8_error::CheckArgCount(args, 1, "createGame")) {
                return;
            }

            if (!v8_error::CheckIsString(args, 0, "name")) {
                return;
            }

            std::string name = v8_convert::ToString(isolate, args[0]);

            // Validate name length
            if (name.length() > 15) {
                v8_error::ThrowError(isolate, "Invalid game name or password length");
                return;
            }

            // Get optional password
            std::string pass;
            if (args.Length() > 1) {
                if (!args[1]->IsString()) {
                    v8_error::ThrowTypeError(isolate, "Invalid arguments specified to createGame");
                    return;
                }
                pass = v8_convert::ToString(isolate, args[1]);
                if (pass.length() > 15) {
                    v8_error::ThrowError(isolate, "Invalid game name or password length");
                    return;
                }
            }

            // Get optional difficulty (0-2, default 3 for highest available)
            int32_t diff = 3;
            if (args.Length() > 2) {
                if (!args[2]->IsNumber()) {
                    v8_error::ThrowTypeError(isolate, "Invalid arguments specified to createGame");
                    return;
                }
                diff = v8_convert::ToInt32(isolate, args[2]);
            }

            if (diff < 0 || diff > static_cast<int32_t>(game::Difficulty::HighestAvailable)) {
                v8_error::ThrowError(isolate, "Invalid difficulty (must be 0-3)");
                return;
            }

            if (!game::CreateGame(name, pass, static_cast<game::Difficulty>(diff))) {
                v8_error::ThrowError(isolate, "createGame failed");
                return;
            }
        });

    /// @description Joins an existing online game at the menu by name and optional password.
    /// @signature joinGame(name: string, password?: string)
    /// @param name {string} - Name of the game to join, max 15 characters.
    /// @param password {string} - Game password, max 15 characters.
    /// @returns {null} - Always null; no-op unless at the menu. Throws on validation or join failure.
    /// @throws {Error} - Game name or password exceeds 15 characters.
    /// @throws {Error} - Join attempt fails.
    v8_function::Register(
        isolate, global, "joinGame", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
            auto* isolate = args.GetIsolate();
            args.GetReturnValue().SetNull();

            // Reference: only works when client is in menu state
            if (game::GetGameState() != game::GameState::Menu) {
                return;
            }

            if (!v8_error::CheckArgCount(args, 1, "joinGame")) {
                return;
            }

            if (!v8_error::CheckIsString(args, 0, "name")) {
                return;
            }

            std::string name = v8_convert::ToString(isolate, args[0]);

            // Validate name length
            if (name.length() > 15) {
                v8_error::ThrowError(isolate, "Invalid game name or password length");
                return;
            }

            // Get optional password
            std::string pass;
            if (args.Length() > 1) {
                if (!args[1]->IsString()) {
                    v8_error::ThrowTypeError(isolate, "Invalid arguments specified to joinGame");
                    return;
                }
                pass = v8_convert::ToString(isolate, args[1]);
                if (pass.length() > 15) {
                    v8_error::ThrowError(isolate, "Invalid game name or password length");
                    return;
                }
            }

            if (!game::JoinGame(name, pass)) {
                v8_error::ThrowError(isolate, "joinGame failed");
                return;
            }
        });

    /// @description Adds or overwrites a stored profile in the d2bs profile config.
    /// @signature addProfile(profileName: string, mode: string, gateway: string, username: string, password: string,
    /// charname: string, spdifficulty?: number)
    /// @param profileName {string} - Profile name (key).
    /// @param mode {string} - Profile mode string; must map to a known profile type.
    /// @param gateway {string} - Realm/gateway.
    /// @param username {string} - Account username; used as the IP address for TCP/IP join profiles.
    /// @param password {string} - Account password.
    /// @param charname {string} - Character name.
    /// @param spdifficulty {number} - Single-player difficulty: 0 = Normal, 1 = Nightmare, 2 = Hell, 3 = highest
    /// available; defaults to 3.
    /// @returns {null} - Always null. Throws on invalid arguments.
    /// @throws {Error} - spdifficulty is outside 0-3.
    /// @throws {Error} - mode string does not map to a known profile type.
    v8_function::Register(
        isolate, global, "addProfile", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
            auto* isolate = args.GetIsolate();

            // Require at least 6 arguments
            if (args.Length() < 6) {
                v8_error::ThrowError(isolate, "Invalid arguments passed to addProfile");
                return;
            }

            // Require no more than 7 arguments
            if (args.Length() > 7) {
                v8_error::ThrowError(isolate, "Invalid arguments passed to addProfile");
                return;
            }

            // Validate all 6 required string arguments
            for (int32_t i = 0; i < 6; i++) {
                if (!args[i]->IsString()) {
                    v8_error::ThrowError(isolate, "Invalid argument passed to addProfile");
                    return;
                }
            }

            std::string profileName = v8_convert::ToString(isolate, args[0]);
            std::string mode = v8_convert::ToString(isolate, args[1]);
            std::string gateway = v8_convert::ToString(isolate, args[2]);
            std::string username = v8_convert::ToString(isolate, args[3]);
            std::string password = v8_convert::ToString(isolate, args[4]);
            std::string charname = v8_convert::ToString(isolate, args[5]);

            // Get optional spdifficulty (default 3). Reject non-number arg consistently
            // with the string-arg validation above - silently dropping it hides script bugs.
            int32_t spdifficulty = 3;
            if (args.Length() == 7) {
                if (!args[6]->IsNumber()) {
                    v8_error::ThrowError(isolate, "Invalid argument passed to addProfile");
                    return;
                }
                spdifficulty = v8_convert::ToInt32(isolate, args[6]);
            }

            // Validate spdifficulty range
            if (spdifficulty < 0 || spdifficulty > static_cast<int32_t>(game::Difficulty::HighestAvailable)) {
                v8_error::ThrowError(isolate, "Invalid argument passed to addProfile");
                return;
            }

            config::ProfileData data;
            data.name = profileName;
            data.type = config::ModeToProfileType(mode);
            // Reference JSMenu.cpp:145 writes the raw mode string verbatim via
            // WritePrivateProfileSectionW. Our path is string -> enum -> string via
            // ProfileTypeToMode, which silently rewrites unknown modes to
            // "invalid". Reject instead to surface the mistake - documented
            // divergence from reference.
            if (data.type == config::ProfileType::Invalid) {
                v8_error::ThrowError(isolate, "Invalid argument passed to addProfile");
                return;
            }
            data.gateway = gateway;
            data.password = password;
            data.character = charname;
            data.difficulty = static_cast<game::Difficulty>(spdifficulty);
            // TcpIpJoin uses the username arg as the IP address (reference Profile.h:12-14 union).
            // IniConfigStore writes ip to both "username" and "ip" keys for backward compat.
            if (data.type == config::ProfileType::TcpIpJoin) {
                data.ip = username;
            } else {
                data.username = username;
            }

            // Return value discarded; reference sets rval=null regardless of
            // whether the profile already existed.
            d2bs::profile::Add(data);
            args.GetReturnValue().SetNull();
        });

    /// @description Returns the current out-of-game menu location id (which menu screen the client is on).
    /// @signature getLocation()
    /// @returns {number|null} - Numeric out-of-game location id when at the menu; null otherwise.
    v8_function::Register(
        isolate, global, "getLocation", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
            // Reference: only works when client is in menu state
            if (game::GetGameState() != game::GameState::Menu) {
                args.GetReturnValue().SetNull();
                return;
            }

            args.GetReturnValue().Set(static_cast<int32_t>(game::GetOutOfGameLocation()));
        });
}

}  // namespace d2bs::api::globals
