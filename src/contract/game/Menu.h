#pragma once

#include <cstdint>
#include <string>

#include "config/ProfileData.h"
#include "game/Types.h"

namespace d2bs::game {

// ============================================================================
// OutOfGame menu state classification
// ============================================================================

// Out-of-game menu state classification. Ordinal values match reference
// OOG_Location (reference/d2bs/Constants.h:75-121) so scripts that compare
// getLocation() against numeric literals keep working.
//
// Framework-defined vocabulary - each game-version port maps its internal
// menu states to these values. States not applicable to a port (e.g., D2R
// has no TCP/IP screens) simply are never returned by that port's
// GetOutOfGameLocation().
enum class OutOfGameLocation : uint32_t {
    // Returned when GetGameState() != GameState::Menu (in-game, loading, title
    // screen) AND when in Menu but no known control pattern matches (transient
    // unknown state). Name matches the most common observed case.
    PreSplash = 0,
    Lobby,
    WaitingInLine,
    LobbyChat,
    LobbyCreateGame,
    LobbyJoinGame,
    LobbyLadder,
    LobbyChannel,
    MainMenu,
    Login,
    LoginError,
    UnableToConnect,
    CharacterSelect,
    RealmDown,
    Disconnected,
    // Char-create screen with a class picked (ord 15) - the OK button is
    // visible (dwState 4 = greyed awaiting name, 5 = clickable). Reference
    // NEW_CHARACTER. The no-class entry screen is CharacterCreate at ord 29.
    CharacterCreateClassSelected,
    CharacterSelectPleaseWait,
    LostConnection,
    SplashScreen,
    CDKeyInUse,
    // Difficulty selection dialog, only shown for single-player character
    // creation flows (Normal/Nightmare/Hell). Does not appear in multiplayer.
    SelectDifficultySinglePlayer,
    MainMenuConnecting,
    InvalidCDKey,
    Connecting,
    ServerDown,
    LobbyPleaseWait,
    GameAlreadyExists,
    Gateway,
    GameDoesNotExist,
    // Char-create entry screen, no class picked yet (ord 29) - the OK button
    // is hidden (dwState 0). Reference CHARACTER_CREATE; the screen reached by
    // clicking "Create New Character" from char-select. Once a class is picked
    // it becomes CharacterCreateClassSelected at ord 15.
    CharacterCreate,
    // Duplicate-name popup when creating a character (ord 30). Reference
    // classifies this via the centered-OK-button pattern; lost-connection,
    // disconnected, and unable-to-connect-TCPIP share the same button but
    // each returns its own distinct ordinal elsewhere in this enum.
    CharacterCreateAlreadyExists,
    AgreeToTerms,
    NewAccount,
    PleaseRead,
    RegisterEmail,
    Credits,
    Cinematics,
    CharacterChangeRealm,
    GameIsFull,
    OtherMultiplayer,
    TcpIp,
    EnterIpAddress,
    CharacterSelectNoChars,
    CharacterSelectChangeRealm,
    UnableToConnectTcpIp,
};

// Pinning asserts - if someone reorders the enum these fire at compile time.
// The integer values are load-bearing for scripts that compare against numeric
// literals.
static_assert(static_cast<uint32_t>(OutOfGameLocation::PreSplash) == 0);
static_assert(static_cast<uint32_t>(OutOfGameLocation::MainMenu) == 8);
static_assert(static_cast<uint32_t>(OutOfGameLocation::Login) == 9);
static_assert(static_cast<uint32_t>(OutOfGameLocation::CharacterCreateClassSelected) == 15);
static_assert(static_cast<uint32_t>(OutOfGameLocation::SplashScreen) == 18);
static_assert(static_cast<uint32_t>(OutOfGameLocation::CharacterCreate) == 29);
static_assert(static_cast<uint32_t>(OutOfGameLocation::CharacterCreateAlreadyExists) == 30);
static_assert(static_cast<uint32_t>(OutOfGameLocation::UnableToConnectTcpIp) == 44);

// ============================================================================
// OutOfGame menu actions
// ============================================================================

// Drive the OutOfGame login state machine for the given profile. Reference:
// reference/d2bs/Profile.cpp:97-296. Returns a LoginResult with Success,
// Timeout, or Error; errorMessage is non-empty on non-Success so callers can
// pass it directly to v8_error::ThrowError.
LoginResult Login(const config::ProfileData& profile);

// Open the BattleNet gateway dropdown and click the entry whose label matches
// `gateway` (case-insensitive substring). Returns true if the gateway is now
// selected (either matched on first read or successfully clicked from the
// dropdown), false on any failure. Reference: reference/d2bs/Control.cpp:240
// (OOG_SelectGateway).
bool SelectGateway(const std::string& gateway);

// Click the named character on the character-select screen. Reference:
// reference/d2bs/Control.cpp:179-228 (OOG_SelectCharacter). Returns true on
// successful click + OK, false on any failure.
bool SelectCharacter(const std::string& charName);

// Create a new character on the char-select screen. Reference:
// reference/d2bs/Control.cpp:134-177 (OOG_CreateCharacter). Returns true on
// success, false on any failure. Reference has no isExpansion parameter -
// LoD-vs-classic isn't part of the JS API surface.
bool CreateCharacter(const std::string& name, CharacterClass charClass, bool isHardcore, bool isLadder);

// Create a Battle.net / open game. Reference:
// reference/d2bs/Control.cpp:434-546 (OOG_CreateGame). Returns true on
// successful Create-button click, false on any failure.
bool CreateGame(const std::string& name, const std::string& password, Difficulty difficulty);

// Join a Battle.net / open game by name. Reference:
// reference/d2bs/Control.cpp:548-588 (OOG_JoinGame). Returns true on
// successful Join-button click, false on any failure.
bool JoinGame(const std::string& name, const std::string& password);

// Classify the current OutOfGame menu screen. Reference:
// reference/d2bs/Control.cpp:319-432 (OOG_GetLocation). Returns
// OutOfGameLocation::PreSplash (value 0) when GetGameState() != GameState::Menu
// or no known control pattern matches.
OutOfGameLocation GetOutOfGameLocation();

}  // namespace d2bs::game
