#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <random>
#include <string>

#include "components/config/AppConfig.h"
#include "components/config/IniConfigStore.h"
#include "components/profile/ProfileData.h"
#include "components/profile/ProfileService.h"

using namespace d2bs::config;

namespace {

// RAII temp INI file - auto-deleted on scope exit. Seeds AppConfig's store so
// ProfileService calls route to the tmp INI.
struct TempIni {
    std::filesystem::path path;

    TempIni() {
        std::random_device rd;
        auto suffix = std::to_string(rd());
        path = std::filesystem::temp_directory_path() / ("d2bsng_profile_test_" + suffix + ".ini");
        // Ensure empty on creation
        std::ofstream(path.string()).close();
    }
    ~TempIni() {
        std::error_code ec;
        std::filesystem::remove(path, ec);
        auto& cfg = GetAppConfig();
        cfg.store.reset();
        cfg.SetProfileName("");
    }

    // Install this file as the active ConfigStore so ProfileService sees it.
    void Install() {
        auto& cfg = GetAppConfig();
        cfg.store = std::make_unique<IniConfigStore>(path);
    }

    void WriteLine(const std::string& line) const {
        std::ofstream out(path.string(), std::ios::app);
        out << line << "\n";
    }
};

}  // namespace

TEST_SUITE("Profile") {
    TEST_CASE("IniConfigStore round-trips SinglePlayer profile") {
        TempIni tmp;
        IniConfigStore store(tmp.path);

        ProfileData in;
        in.name = "sp1";
        in.type = ProfileType::SinglePlayer;
        in.character = "Hero";
        in.difficulty = d2bs::game::Difficulty::Hell;
        store.SaveProfile(in);

        auto out = store.LoadProfile("sp1");
        REQUIRE(out.has_value());
        // NOLINTBEGIN(bugprone-unchecked-optional-access) - REQUIRE above guarantees has_value
        CHECK(out->name == "sp1");
        CHECK(out->type == ProfileType::SinglePlayer);
        CHECK(out->character == "Hero");
        CHECK(out->difficulty == d2bs::game::Difficulty::Hell);
        // NOLINTEND(bugprone-unchecked-optional-access)
    }

    TEST_CASE("IniConfigStore round-trips BattleNet profile") {
        TempIni tmp;
        IniConfigStore store(tmp.path);

        ProfileData in;
        in.name = "bn1";
        in.type = ProfileType::BattleNet;
        in.username = "acct";
        in.password = "pw";
        in.character = "Hero";
        in.gateway = "US East";
        store.SaveProfile(in);

        auto out = store.LoadProfile("bn1");
        REQUIRE(out.has_value());
        // NOLINTBEGIN(bugprone-unchecked-optional-access) - REQUIRE above guarantees has_value
        CHECK(out->type == ProfileType::BattleNet);
        CHECK(out->username == "acct");
        CHECK(out->password == "pw");
        CHECK(out->gateway == "US East");
        // NOLINTEND(bugprone-unchecked-optional-access)
    }

    TEST_CASE("IniConfigStore legacy TCP/IP join - ip read from username when absent") {
        TempIni tmp;
        // Old-style INI: no "ip" key, IP stored in "username" column.
        tmp.WriteLine("[tcp]");
        tmp.WriteLine("mode=join");
        tmp.WriteLine("character=Joiner");
        tmp.WriteLine("username=192.168.1.1");
        tmp.WriteLine("password=");
        tmp.WriteLine("gateway=");
        tmp.WriteLine("spdifficulty=0");

        IniConfigStore store(tmp.path);
        auto out = store.LoadProfile("tcp");
        REQUIRE(out.has_value());
        // NOLINTBEGIN(bugprone-unchecked-optional-access) - REQUIRE above guarantees has_value
        CHECK(out->type == ProfileType::TcpIpJoin);
        CHECK(out->ip == "192.168.1.1");
        // NOLINTEND(bugprone-unchecked-optional-access)
    }

    TEST_CASE("IniConfigStore SaveProfile writes IP to both username and ip keys for TcpIpJoin") {
        TempIni tmp;
        IniConfigStore store(tmp.path);

        ProfileData in;
        in.name = "tcp";
        in.type = ProfileType::TcpIpJoin;
        in.character = "Joiner";
        in.ip = "10.0.0.5";
        store.SaveProfile(in);

        auto out = store.LoadProfile("tcp");
        REQUIRE(out.has_value());
        // NOLINTBEGIN(bugprone-unchecked-optional-access) - REQUIRE above guarantees has_value
        CHECK(out->type == ProfileType::TcpIpJoin);
        CHECK(out->ip == "10.0.0.5");
        // NOLINTEND(bugprone-unchecked-optional-access)
    }

    TEST_CASE("ModeToProfileType covers all modes") {
        CHECK(ModeToProfileType("single") == ProfileType::SinglePlayer);
        CHECK(ModeToProfileType("battlenet") == ProfileType::BattleNet);
        CHECK(ModeToProfileType("open") == ProfileType::OpenBattleNet);
        CHECK(ModeToProfileType("host") == ProfileType::TcpIpHost);
        CHECK(ModeToProfileType("join") == ProfileType::TcpIpJoin);
        CHECK(ModeToProfileType("") == ProfileType::Invalid);
        // Reference only matches on the first letter (reference/d2bs/Profile.cpp:40-56).
        // "banana" starts with 'b' so it maps to BattleNet - preserved behavior.
        CHECK(ModeToProfileType("banana") == ProfileType::BattleNet);
        // Unknown first letter -> Invalid.
        CHECK(ModeToProfileType("xyz") == ProfileType::Invalid);
    }

    TEST_CASE("ProfileTypeToMode round-trip") {
        CHECK(ProfileTypeToMode(ProfileType::SinglePlayer) == "single");
        CHECK(ProfileTypeToMode(ProfileType::BattleNet) == "battlenet");
        CHECK(ProfileTypeToMode(ProfileType::OpenBattleNet) == "open");
        CHECK(ProfileTypeToMode(ProfileType::TcpIpHost) == "host");
        CHECK(ProfileTypeToMode(ProfileType::TcpIpJoin) == "join");
        CHECK(ProfileTypeToMode(ProfileType::Invalid) == "invalid");
    }

    TEST_CASE("ProfileService::Load returns nullopt for unknown profile") {
        TempIni tmp;
        tmp.Install();
        auto out = d2bs::profile::Load("nonexistent");
        CHECK(!out.has_value());
    }

    TEST_CASE("ProfileService::LoadActive returns nullopt when no active profile") {
        TempIni tmp;
        tmp.Install();
        GetAppConfig().SetProfileName("");
        auto out = d2bs::profile::LoadActive();
        CHECK(!out.has_value());
    }

    TEST_CASE("ProfileService::Add writes new profile; idempotent for existing") {
        TempIni tmp;
        tmp.Install();

        ProfileData data;
        data.name = "added";
        data.type = ProfileType::SinglePlayer;
        data.character = "Hero";
        data.difficulty = d2bs::game::Difficulty::Nightmare;

        CHECK(d2bs::profile::Add(data) == true);

        // Second add is a no-op (returns false, doesn't overwrite)
        ProfileData data2 = data;
        data2.character = "Other";
        CHECK(d2bs::profile::Add(data2) == false);

        // Confirm original is preserved
        auto loaded = d2bs::profile::Load("added");
        REQUIRE(loaded.has_value());
        // NOLINTNEXTLINE(bugprone-unchecked-optional-access) - REQUIRE above guarantees has_value
        CHECK(loaded->character == "Hero");
    }

    TEST_CASE("ProfileService::Add rejects empty name") {
        TempIni tmp;
        tmp.Install();
        ProfileData data;
        data.name = "";
        data.type = ProfileType::SinglePlayer;
        CHECK(d2bs::profile::Add(data) == false);
    }

    TEST_CASE("ProfileService::ResolveCharacter returns character for existing profile") {
        TempIni tmp;
        tmp.Install();

        ProfileData data;
        data.name = "resolve1";
        data.type = ProfileType::SinglePlayer;
        data.character = "HeroName";
        d2bs::profile::Add(data);

        auto result = d2bs::profile::ResolveCharacter("resolve1");
        REQUIRE(result.has_value());
        CHECK(*result == "HeroName");
    }

    TEST_CASE("ProfileService::ResolveCharacter returns nullopt for unknown profile") {
        TempIni tmp;
        tmp.Install();
        auto result = d2bs::profile::ResolveCharacter("never_added");
        CHECK(!result.has_value());
    }

    TEST_CASE("ProfileService::ResolveCharacter returns nullopt for empty character") {
        TempIni tmp;
        tmp.Install();

        ProfileData data;
        data.name = "empty_char";
        data.type = ProfileType::SinglePlayer;
        data.character = "";
        d2bs::profile::Add(data);

        auto result = d2bs::profile::ResolveCharacter("empty_char");
        CHECK(!result.has_value());
    }

    TEST_CASE("ProfileService::Switch updates active profile name") {
        TempIni tmp;
        tmp.Install();

        ProfileData data;
        data.name = "sw1";
        data.type = ProfileType::SinglePlayer;
        d2bs::profile::Add(data);

        CHECK(d2bs::profile::Switch("sw1") == true);
        CHECK(GetAppConfig().GetProfileName() == "sw1");
    }

    TEST_CASE("ProfileService::Switch fails for unknown profile, name unchanged") {
        TempIni tmp;
        tmp.Install();
        GetAppConfig().SetProfileName("original");

        CHECK(d2bs::profile::Switch("nonexistent") == false);
        CHECK(GetAppConfig().GetProfileName() == "original");
    }

    TEST_CASE("ProfileService::Switch preserves user-supplied profile name case") {
        TempIni tmp;
        tmp.Install();

        ProfileData data;
        data.name = "foo";
        data.type = ProfileType::SinglePlayer;
        d2bs::profile::Add(data);

        // Win32 INI section lookup is case-insensitive, so Switch("FOO") finds
        // the [foo] section. The stored name preserves the caller's casing;
        // case-insensitive comparison happens at the snapshot-diff layer.
        CHECK(d2bs::profile::Switch("FOO") == true);
        CHECK(GetAppConfig().GetProfileName() == "FOO");
    }

    TEST_CASE("IniConfigStore::ProfileExists detects existing profile (case-insensitive)") {
        TempIni tmp;
        IniConfigStore store(tmp.path);

        ProfileData data;
        data.name = "MixedCase";
        data.type = ProfileType::SinglePlayer;
        store.SaveProfile(data);

        CHECK(store.ProfileExists("MixedCase"));
        CHECK(store.ProfileExists("mixedcase"));
        CHECK(!store.ProfileExists("MissingProfile"));
    }

    TEST_CASE("IniConfigStore::ListProfiles excludes [settings]") {
        TempIni tmp;
        tmp.WriteLine("[settings]");
        tmp.WriteLine("MaxGameTime=0");
        tmp.WriteLine("[prof1]");
        tmp.WriteLine("mode=single");
        tmp.WriteLine("[prof2]");
        tmp.WriteLine("mode=single");

        IniConfigStore store(tmp.path);
        auto profiles = store.ListProfiles();
        CHECK(profiles.size() == 2);
        // Ordering not guaranteed by the API contract - check membership.
        bool hasProf1 = false;
        bool hasProf2 = false;
        for (const auto& p : profiles) {
            if (p == "prof1")
                hasProf1 = true;
            if (p == "prof2")
                hasProf2 = true;
        }
        CHECK(hasProf1);
        CHECK(hasProf2);
    }
}
