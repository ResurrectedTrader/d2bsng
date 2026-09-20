#include "MenuFunctions.h"

#include <string>

#include "ArgChecks.h"
#include "config/AppConfig.h"
#include "config/ProfileData.h"
#include "game/GameHelpers.h"
#include "game/Menu.h"
#include "profile/ProfileService.h"
#include "scripting/Args.h"

namespace d2bs::api::globals {

void RegisterMenuFunctions(script::Registry& registry) {
    /// @description Logs in at the menu using a stored profile, driving the UI through character selection.
    /// @signature login()
    /// @signature login(profileName: string)
    /// @param profileName {string} - Name of the stored profile; defaults to the configured app profile.
    /// @returns {undefined} - No value; no-op unless at the menu. Throws on missing/invalid profile or login failure.
    /// @throws {Error} - Profile name resolves empty (none passed, none configured).
    /// @throws {Error} - Named profile does not exist.
    /// @throws {Error} - Login attempt fails.
    registry.Global(
        "login", +[](script::Args& args) {
            // Reference: only works when client is in menu state
            if (game::GetGameState() != game::GameState::Menu) {
                return true;
            }

            std::string profileName;
            if (args.Count() > 0 && args.IsString(0)) {
                profileName = args.String(0).value_or(std::string{});
            } else {
                profileName = config::GetAppConfig().GetProfileName();
                if (profileName.empty()) {
                    args.Throw(script::ErrorKind::Error, "Invalid profile specified!");
                    return false;
                }
            }

            auto profile = services::profile::Load(profileName);
            if (!profile) {
                args.Throw(script::ErrorKind::Error, "Profile does not exist!");
                return false;
            }

            // Set name only on success: reference sets it before ProfileExists, leaving szProfile
            // pointing at a missing profile after a failed login. We defer to avoid that.
            config::GetAppConfig().SetProfileName(profileName);

            auto result = game::Login(*profile);
            if (result.status != game::LoginStatus::Success) {
                args.Throw(script::ErrorKind::Error, result.errorMessage);
                return false;
            }
            return true;
        });

    /// @description Selects a profile's character in the character-selection screen.
    /// @signature selectCharacter(profileName: string)
    /// @param profileName {string} - Name of the stored profile whose character should be selected.
    /// @returns {boolean} - true if the character was selected, false otherwise.
    /// @throws {Error} - Profile cannot be resolved to a character.
    registry.Global(
        "selectCharacter", +[](script::Args& args) {
            // Reference JSMenu.cpp:45-46 throws one message for both wrong arg
            // count and wrong arg type; reproduce verbatim so scripts that
            // string-match the error keep working.
            if (args.Count() != 1 || !args.IsString(0)) {
                args.Throw(script::ErrorKind::Error, "Invalid parameters specified to selectCharacter");
                return false;
            }

            auto charname = services::profile::ResolveCharacter(args.String(0).value_or(std::string{}));
            if (!charname) {
                args.Throw(script::ErrorKind::Error, "Invalid profile specified");
                return false;
            }
            args.SetReturnValue(game::SelectCharacter(*charname));
            return true;
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
    registry.Global(
        "createCharacter", +[](script::Args& args) {
            // Reference: only works when client is in menu state
            if (game::GetGameState() != game::GameState::Menu) {
                return true;
            }

            if (args.Count() < 2) {
                args.Throw(script::ErrorKind::Error, "createCharacter requires at least 2 arguments");
                return false;
            }
            if (!CheckIsString(args, 0, "name") || !CheckIsNumber(args, 1, "type")) {
                return false;
            }

            const std::string name = args.String(0).value_or(std::string{});
            const int32_t type = args.Int32(1).value_or(0);

            bool hardcore = false;
            if (args.Count() > 2 && args.IsBool(2)) {
                hardcore = args.Bool(2).value_or(false);
            }

            bool ladder = false;
            if (args.Count() > 3 && args.IsBool(3)) {
                ladder = args.Bool(3).value_or(false);
            }

            // Validate character class (0-6)
            if (type < 0 || type > static_cast<int32_t>(game::CharacterClass::Assassin)) {
                args.Throw(script::ErrorKind::Error, "Invalid character type");
                return false;
            }

            args.SetReturnValue(game::CreateCharacter(name, static_cast<game::CharacterClass>(type), hardcore, ladder));
            return true;
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
    registry.Global(
        "createGame", +[](script::Args& args) {
            args.SetReturnValueNull();

            // Reference: only works when client is in menu state
            if (game::GetGameState() != game::GameState::Menu) {
                return true;
            }

            if (!CheckArgCount(args, 1, "createGame") || !CheckIsString(args, 0, "name")) {
                return false;
            }

            const std::string name = args.String(0).value_or(std::string{});
            if (name.length() > 15) {
                args.Throw(script::ErrorKind::Error, "Invalid game name or password length");
                return false;
            }

            std::string pass;
            if (args.Count() > 1) {
                if (!args.IsString(1)) {
                    args.Throw(script::ErrorKind::TypeError, "Invalid arguments specified to createGame");
                    return false;
                }
                pass = args.String(1).value_or(std::string{});
                if (pass.length() > 15) {
                    args.Throw(script::ErrorKind::Error, "Invalid game name or password length");
                    return false;
                }
            }

            int32_t diff = static_cast<int32_t>(game::Difficulty::HighestAvailable);
            if (args.Count() > 2) {
                if (!args.IsNumber(2)) {
                    args.Throw(script::ErrorKind::TypeError, "Invalid arguments specified to createGame");
                    return false;
                }
                diff = args.Int32(2).value_or(diff);
            }

            if (diff < 0 || diff > static_cast<int32_t>(game::Difficulty::HighestAvailable)) {
                args.Throw(script::ErrorKind::Error, "Invalid difficulty (must be 0-3)");
                return false;
            }

            if (!game::CreateGame(name, pass, static_cast<game::Difficulty>(diff))) {
                args.Throw(script::ErrorKind::Error, "createGame failed");
                return false;
            }
            return true;
        });

    /// @description Joins an existing online game at the menu by name.
    /// @signature joinGame(name: string, password?: string)
    /// @param name {string} - Game name, max 15 characters.
    /// @param password {string} - Game password, max 15 characters.
    /// @returns {null} - Always null; no-op unless at the menu. Throws on validation or join failure.
    /// @throws {Error} - Game name or password exceeds 15 characters.
    /// @throws {Error} - Join attempt fails.
    registry.Global(
        "joinGame", +[](script::Args& args) {
            args.SetReturnValueNull();

            // Reference: only works when client is in menu state
            if (game::GetGameState() != game::GameState::Menu) {
                return true;
            }

            if (!CheckArgCount(args, 1, "joinGame") || !CheckIsString(args, 0, "name")) {
                return false;
            }

            const std::string name = args.String(0).value_or(std::string{});
            if (name.length() > 15) {
                args.Throw(script::ErrorKind::Error, "Invalid game name or password length");
                return false;
            }

            std::string pass;
            if (args.Count() > 1) {
                if (!args.IsString(1)) {
                    args.Throw(script::ErrorKind::TypeError, "Invalid arguments specified to joinGame");
                    return false;
                }
                pass = args.String(1).value_or(std::string{});
                if (pass.length() > 15) {
                    args.Throw(script::ErrorKind::Error, "Invalid game name or password length");
                    return false;
                }
            }

            if (!game::JoinGame(name, pass)) {
                args.Throw(script::ErrorKind::Error, "joinGame failed");
                return false;
            }
            return true;
        });

    /// @description Lists the custom Battle.net gateways injected into the realm list.
    /// @signature getRealms()
    /// @returns {Array<{name:string,host:string}>} - one entry per registered realm
    registry.Global(
        "getRealms", +[](script::Args& args) {
            const auto realms = game::GetRealms();
            auto array = args.NewArray(realms.size());
            size_t i = 0;
            for (const auto& realm : realms) {
                auto entry = args.NewObject();
                entry.Set("name", realm.name).Set("host", realm.host);
                array.Set(i++, entry);
            }
            args.SetReturnValue(array);
            return true;
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
    registry.Global(
        "addProfile", +[](script::Args& args) {
            // Require at least 6 and no more than 7 arguments
            if (args.Count() < 6 || args.Count() > 7) {
                args.Throw(script::ErrorKind::Error, "Invalid arguments passed to addProfile");
                return false;
            }

            // Validate all 6 required string arguments
            for (size_t i = 0; i < 6; i++) {
                if (!args.IsString(i)) {
                    args.Throw(script::ErrorKind::Error, "Invalid argument passed to addProfile");
                    return false;
                }
            }

            const std::string profileName = args.String(0).value_or(std::string{});
            const std::string mode = args.String(1).value_or(std::string{});
            const std::string gateway = args.String(2).value_or(std::string{});
            const std::string username = args.String(3).value_or(std::string{});
            const std::string password = args.String(4).value_or(std::string{});
            const std::string charname = args.String(5).value_or(std::string{});

            // Get optional spdifficulty (default 3). Reject non-number arg consistently
            // with the string-arg validation above - silently dropping it hides script bugs.
            int32_t spdifficulty = static_cast<int32_t>(game::Difficulty::HighestAvailable);
            if (args.Count() == 7) {
                if (!args.IsNumber(6)) {
                    args.Throw(script::ErrorKind::Error, "Invalid argument passed to addProfile");
                    return false;
                }
                spdifficulty = args.Int32(6).value_or(spdifficulty);
            }

            // Validate spdifficulty range
            if (spdifficulty < 0 || spdifficulty > static_cast<int32_t>(game::Difficulty::HighestAvailable)) {
                args.Throw(script::ErrorKind::Error, "Invalid argument passed to addProfile");
                return false;
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
                args.Throw(script::ErrorKind::Error, "Invalid argument passed to addProfile");
                return false;
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
            services::profile::Add(data);
            args.SetReturnValueNull();
            return true;
        });

    /// @description Returns the current out-of-game menu location id (which menu screen the client is on).
    /// @signature getLocation()
    /// @returns {number|null} - Numeric out-of-game location id when at the menu; null otherwise.
    registry.Global(
        "getLocation", +[](script::Args& args) {
            // Reference: only works when client is in menu state
            if (game::GetGameState() != game::GameState::Menu) {
                args.SetReturnValueNull();
                return true;
            }

            args.SetReturnValue(static_cast<int32_t>(game::GetOutOfGameLocation()));
            return true;
        });
}

}  // namespace d2bs::api::globals
