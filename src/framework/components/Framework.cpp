#include "Framework.h"

#include <spdlog/spdlog.h>
#include <exception>
#include <filesystem>
#include <memory>

#include "components/characterstate/CharacterState.h"
#include "components/config/AppConfig.h"
#include "components/config/IniConfigStore.h"
#include "components/config/ScriptPaths.h"
#include "components/config/Version.h"
#include "components/console/ConsoleSink.h"
#include "components/dde/DdeService.h"
#include "components/drawing/Drawable.h"
#include "components/events/EventDispatch.h"
#include "components/gameloop/GameLoop.h"
#include "components/profile/ProfileService.h"
#include "components/script/Commands.h"
#include "components/script/ScriptEngine.h"
#include "components/update/UpdateChecker.h"
#include "game/Bridge.h"
#include "game/Console.h"
#include "game/GameCallbacks.h"
#include "game/GameHelpers.h"
#include "utils/DeferGuard.h"
#include "utils/threadutils.h"

namespace d2bs {

void Framework::Initialize(HMODULE hModule) {
    // Early logger - SetupLogging() replaces it; any failure here is still reportable.
    logger_ = spdlog::default_logger();
    // Spawn init on a separate thread to avoid heavy work under the DLL loader lock.
    initThread_ = std::jthread([hModule]() { Framework::DoInitialize(hModule); });
}

void Framework::DoInitialize(HMODULE hModule) {
    d2bs::thread_utils::SetThreadDescription("d2bs framework init");
    try {
        SetupPaths(hModule);
        SetupLogging();

        // Register crash handler early so any subsequent failure produces diagnostics
        previousExceptionFilter_ = SetUnhandledExceptionFilter(d2bs::thread_utils::ExceptionHandler);

        // VEH backstop: V8/Crashpad and Detours both install their own UEFs;
        // whichever runs last wins, so our SEH filter may never see the
        // crash. The VEH fires first-chance and is purely diagnostic - it
        // logs and propagates so existing dispatch is unaffected. Pass
        // first=1 so we run before any other VEHs that get registered later.
        vectoredExceptionHandle_ =
            AddVectoredExceptionHandler(/*FirstHandler=*/1, &d2bs::thread_utils::VectoredExceptionHandler);

        // Wire the crash hook: pop the console visible on crash. The
        // console's render thread is independent of the game thread, so
        // the dump stays readable even if the game is hung in HANG_ON_CRASH
        // mode. utils/ can't depend on framework/game, hence the indirection.
        d2bs::thread_utils::onCrashFunction.store(&game::console::Show, std::memory_order_release);

        LoadConfig();

        logger_->info("d2bsng v{} initializing", D2BS_VERSION);

        // Bridge::Init is idempotent - the DLL entry path already invoked it
        // synchronously, so this call just returns the cached success value.
        // In the CLI port the framework never reaches this codepath (the CLI
        // wires up its own bridge in main()). Either way, a false return here
        // means the port's bridge has refused to come up, in which case the
        // rest of init would dereference unresolved pointers.
        if (!game::Bridge::Init()) {
            logger_->error("game::Bridge::Init() returned false - aborting framework init");
            return;
        }
        game::InstallHooks(BuildCallbacks());

        // Resolve launch-time profile. Reference: reference/d2bs/Helpers.cpp:80-105.
        if (auto launchProfile = game::GetLaunchProfile()) {
            if (d2bs::profile::Switch(*launchProfile)) {
                logger_->info("Switched to profile '{}'", *launchProfile);
            } else {
                logger_->warn("Profile '{}' not found", *launchProfile);
            }
        }

        ScriptEngine::Instance().Initialize();

        // Mirrors reference/d2bs/dde.cpp DdeCallback:
        //   Execute -> ScriptEngine::RunCommand.
        //   Poke    -> profile::Switch.
        dde::DdeService::Instance().Start(
            [](dde::Transaction txn, std::string_view topic, std::string_view item, std::string_view data) {
                switch (txn) {
                    case dde::Transaction::Evaluate:
                        ScriptEngine::Instance().Evaluate(std::string(data));
                        break;
                    case dde::Transaction::Poke: {
                        auto name = std::string(data);
                        if (d2bs::profile::Switch(name)) {
                            spdlog::info("DDE profile switch: '{}'", name);
                        } else {
                            spdlog::warn("DDE profile switch failed for '{}' (profile does not exist)", name);
                        }
                        break;
                    }
                    case dde::Transaction::Request:
                        // Unreachable: XTYP_REQUEST is rejected at the DDE layer by CBF_FAIL_REQUESTS.
                        break;
                }
            });

        // Best-effort background update check (polls GitHub releases every 6h;
        // the game loop surfaces a notice on game entry). Independent of game
        // readiness, so it can start as soon as the framework is up.
        framework::update::UpdateChecker::Instance().Start();

        logger_->info("d2bsng initialized");
    } catch (const std::exception& ex) {
        logger_->error("Framework::Initialize failed: {}", ex.what());
    } catch (...) {
        logger_->error("Framework::Initialize failed with unknown exception");
    }
}

void Framework::Shutdown() {
    // Wait for init to complete before tearing down
    if (initThread_.joinable()) {
        initThread_.join();
    }

    // Restore previous exception filter on every exit path so unmapped code isn't
    // called after unload, even if a shutdown step throws. Also remove the
    // VEH - failing to do so would leave a dangling callback once the DLL
    // unmaps.
    DeferGuard restoreFilter([] {
        SetUnhandledExceptionFilter(previousExceptionFilter_);
        if (vectoredExceptionHandle_ != nullptr) {
            RemoveVectoredExceptionHandler(vectoredExceptionHandle_);
            vectoredExceptionHandle_ = nullptr;
        }
    });

    try {
        logger_->info("d2bsng shutting down");

        // Stop the DDE service before tearing down other subsystems so that any
        // in-flight DDE handler call finishes against a still-valid framework.
        dde::DdeService::Instance().Stop();

        // Halt the background update poller (joins its thread) before the rest
        // of teardown so no network work outlives the framework.
        framework::update::UpdateChecker::Instance().Stop();

        ScriptEngine::Instance().Shutdown();
        game::RemoveHooks();
        game::Bridge::Shutdown();

        logger_->info("d2bsng shutdown complete");
        spdlog::shutdown();
    } catch (const std::exception& ex) {
        logger_->error("Framework::Shutdown failed: {}", ex.what());
        spdlog::shutdown();
    } catch (...) {
        logger_->error("Framework::Shutdown failed with unknown exception");
        spdlog::shutdown();
    }
}

void Framework::SetupPaths(HMODULE hModule) {
    // Resolve DLL directory from the module handle.
    std::array<wchar_t, MAX_PATH> dllPath{};
    GetModuleFileNameW(hModule, dllPath.data(), MAX_PATH);
    auto basePath = std::filesystem::path(dllPath.data()).parent_path();

    // Seed AppConfig.scriptPaths.basePath before LoadConfig() so GetPathRelScript()
    // and INI resolution have a valid base. LoadSettings() overwrites all four
    // ScriptPaths fields from the [settings] section immediately after.
    auto& appConfig = config::GetAppConfig();
    config::ScriptPaths paths;
    paths.basePath = basePath;
    appConfig.SetScriptPaths(std::move(paths));
}

void Framework::SetupLogging() {
    // Framework installs only the ConsoleSink: every framework-internal log
    // entry fans out to game::console::OnMessage with source=Log. File /
    // stderr / network sinks are a port concern. Ports
    // that want conventional log files push their own sink onto
    // spdlog::default_logger()->sinks() during their own init, or write to
    // disk from inside OnMessage.
    auto consoleSink = std::make_shared<framework::console::ConsoleSink>();
    logger_ = std::make_shared<spdlog::logger>("d2bs", consoleSink);
    logger_->set_level(spdlog::level::debug);
    logger_->flush_on(spdlog::level::info);

    spdlog::set_default_logger(logger_);
}

void Framework::LoadConfig() {
    auto& appConfig = config::GetAppConfig();
    auto iniPath = appConfig.GetScriptPaths().basePath / "d2bs.ini";

    appConfig.store = std::make_unique<config::IniConfigStore>(iniPath);
    appConfig.store->LoadSettings(appConfig);
}

game::GameCallbacks Framework::BuildCallbacks() {
    game::GameCallbacks callbacks;

    // --- Input (blockable) ---
    callbacks.onKeyEvent = &KeyDownUpEventDispatch;

    callbacks.onMouseClick = +[](game::ClickButton button, game::Position pos, game::KeyState state) -> bool {
        bool blocked = framework::drawing::Drawable::OnClick(button, pos.ToPoint(), game::GetGameState());
        MouseClickEventDispatch(button, pos, state);
        return blocked;
    };

    callbacks.onMouseMove = +[](game::Position pos) {
        framework::drawing::Drawable::OnMouseMove(pos.ToPoint(), game::GetGameState());
        MouseMoveEventDispatch(pos);
    };

    // --- Chat (blockable) ---
    callbacks.onChatMessage = &ChatEventDispatch;
    callbacks.onChatInput = &ChatInputEventDispatch;
    callbacks.onWhisper = &WhisperEventDispatch;

    // Overlay/terminal Enter -> RunCommand dispatch. Fire-and-forget.
    callbacks.onConsoleInput = &framework::script::RunCommand;

    // --- Packets (blockable) ---
    callbacks.onGamePacketReceived = &GamePacketEventDispatch;
    callbacks.onGamePacketSent = &GamePacketSentEventDispatch;
    callbacks.onRealmPacket = &RealmPacketEventDispatch;

    // --- Game lifecycle ---
    callbacks.onGameEvent = &GameActionEventDispatch;
    callbacks.onItemAction = &ItemActionEventDispatch;

    // Observed monster deaths feed the character-state kill counter. Runs on the
    // game thread (death packet hook) where the frame write lock is held, so the
    // Unit::Find inside RecordKill resolves lock-free (see game/GameLock.h).
    callbacks.onMonsterDeath = +[](uint32_t unitId) {
        framework::characterstate::CharacterState::Instance().RecordKill(unitId);
    };

    // --- IPC ---
    // Reference D2Handlers.cpp:184-196 intercepts two reserved WM_COPYDATA
    // dwData values exclusively (no fall-through to CopyDataEvent):
    //   IpcMode::Evaluate      (0x1337)  - run payload as JS via ScriptEngine.
    //   IpcMode::SwitchProfile (0x31337) - set active profile via profile::Switch.
    callbacks.onIPC = +[](game::IpcMode mode, const std::string& payload) {
        switch (mode) {
            case game::IpcMode::Evaluate:
                ScriptEngine::Instance().Evaluate(payload);
                return;
            case game::IpcMode::SwitchProfile:
                if (d2bs::profile::Switch(payload)) {
                    spdlog::info("IPC profile switch: '{}'", payload);
                } else {
                    spdlog::warn("IPC profile switch failed for '{}' (profile does not exist)", payload);
                }
                return;
        }
        // Manager handover: D2BotNG re-registers its message window by sending a
        // WM_COPYDATA whose dwData is the new HWND and whose payload is the literal
        // "Handle". Track it as the IPC target for engine-side senders (character
        // state). Still forwarded to script listeners below so JS handlers see it.
        if (payload == "Handle") {
            config::GetAppConfig().managerHandle.store(static_cast<uintptr_t>(mode), std::memory_order_relaxed);
        }
        // Not a reserved mode - pass through to script listeners.
        CopyDataEventDispatch(mode, payload);
    };

    // --- Rendering ---
    callbacks.onSleep = +[](std::chrono::milliseconds duration) {
        framework::gameloop::GameLoop::Instance().OnSleep(duration);
    };
    callbacks.onDraw = +[]() {
        framework::gameloop::GameLoop::Instance().OnDraw();
    };

    return callbacks;
}

}  // namespace d2bs
