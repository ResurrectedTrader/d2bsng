#include "Host.h"

#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/spdlog.h>

#include <cctype>
#include <chrono>
#include <exception>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

#include "analytics/Analytics.h"
#include "characterstate/CharacterState.h"
#include "components/console/Console.h"
#include "components/console/ConsoleSink.h"
#include "components/drawing/Drawable.h"
#include "components/events/EventDispatch.h"
#include "components/gameloop/GameLoop.h"
#include "components/script/Commands.h"
#include "components/script/ScriptEngine.h"
#include "config/AppConfig.h"
#include "config/CompatibilityFlags.h"
#include "config/IniConfigStore.h"
#include "config/ScriptPaths.h"
#include "config/Version.h"
#include "dde/DdeService.h"
#include "game/Bridge.h"
#include "game/Compatibility.h"
#include "game/Console.h"
#include "game/GameCallbacks.h"
#include "game/GameHelpers.h"
#include "profile/ProfileService.h"
#include "update/UpdateChecker.h"
#include "utils/DeferGuard.h"
#include "utils/threadutils.h"
#include "utils/utils.h"

namespace d2bs::runtime {

void Host::Initialize(HMODULE hModule) {
    // Claim the logger before SetupLogging so a failure on the way there is still
    // reportable; it starts sinkless and picks up the sinks SetupLogging installs.
    logger_ = utils::GetLogger("d2bs");
    // Spawn init on a separate thread to avoid heavy work under the DLL loader lock.
    initThread_ = std::jthread([hModule]() { DoInitialize(hModule); });
}

void Host::DoInitialize(HMODULE hModule) {
    thread_utils::SetThreadDescription("d2bs framework init");
    try {
        SetupPaths(hModule);
        SetupLogging();

        // Register crash handler early so any subsequent failure produces diagnostics
        previousExceptionFilter_ = SetUnhandledExceptionFilter(thread_utils::ExceptionHandler);

        // VEH backstop: V8/Crashpad and Detours both install their own UEFs;
        // whichever runs last wins, so our SEH filter may never see the
        // crash. The VEH fires first-chance and is purely diagnostic - it
        // logs and propagates so existing dispatch is unaffected. Pass
        // first=1 so we run before any other VEHs that get registered later.
        vectoredExceptionHandle_ =
            AddVectoredExceptionHandler(/*FirstHandler=*/1, &thread_utils::VectoredExceptionHandler);

        // Wire the crash hook: pop the console visible on crash. The
        // console's render thread is independent of the game thread, so
        // the dump stays readable even if the game is hung in HANG_ON_CRASH
        // mode. utils/ can't depend on framework/game, hence the indirection.
        thread_utils::onCrashFunction.store(&game::console::Show, std::memory_order_release);

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
        if (!game::InstallHooks(BuildCallbacks())) {
            logger_->error("game::InstallHooks() returned false - aborting framework init");
            return;
        }

        // Build the compatibility-flag registry before any script runs: the
        // framework's built-in flags, then any the game-version port contributes
        // (see docs/compatibility.md). All default to enabled.
        config::CompatibilityFlags::Instance().RegisterDefaults();
        for (const auto& flag : game::GetCompatibilityFlags()) {
            config::CompatibilityFlags::Instance().Register(flag);
        }

        // Resolve launch-time profile. Reference: reference/d2bs/Helpers.cpp:80-105.
        if (auto launchProfile = game::GetLaunchProfile()) {
            if (services::profile::Switch(*launchProfile)) {
                logger_->info("Switched to profile '{}'", *launchProfile);
            } else {
                logger_->warn("Profile '{}' not found", *launchProfile);
            }
        }

        ScriptEngine::Instance().Initialize();

        // Mirrors reference/d2bs/dde.cpp DdeCallback:
        //   Execute -> ScriptEngine::RunCommand.
        //   Poke    -> services::profile::Switch.
        services::dde::DdeService::Instance().Start(
            [](services::dde::Transaction txn, std::string_view topic, std::string_view item, std::string_view data) {
                switch (txn) {
                    case services::dde::Transaction::Evaluate:
                        ScriptEngine::Instance().Evaluate(std::string(data));
                        break;
                    case services::dde::Transaction::Poke: {
                        auto name = std::string(data);
                        if (services::profile::Switch(name)) {
                            logger_->info("DDE profile switch: '{}'", name);
                        } else {
                            logger_->warn("DDE profile switch failed for '{}' (profile does not exist)", name);
                        }
                        break;
                    }
                    case services::dde::Transaction::Request:
                        // Unreachable: XTYP_REQUEST is rejected at the DDE layer by CBF_FAIL_REQUESTS.
                        break;
                }
            });

        // Best-effort background update check (polls GitHub releases every 6h;
        // the game loop surfaces a notice on game entry). Independent of game
        // readiness, so it can start as soon as the framework is up.
        services::update::UpdateChecker::Instance().Start();

        // Best-effort anonymous usage analytics: a single startup event to
        // Aptabase, off unless an app key is configured. Independent of game
        // readiness. See docs/analytics.md.
        services::analytics::Analytics::Instance().Start();

        logger_->info("d2bsng initialized");
    } catch (const std::exception& ex) {
        logger_->error("Host::Initialize failed: {}", ex.what());
    } catch (...) {
        logger_->error("Host::Initialize failed with unknown exception");
    }
}

void Host::Shutdown() {
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
        services::dde::DdeService::Instance().Stop();

        // Halt the background update poller (joins its thread) before the rest
        // of teardown so no network work outlives the framework.
        services::update::UpdateChecker::Instance().Stop();
        services::analytics::Analytics::Instance().Stop();

        ScriptEngine::Instance().Shutdown();
        game::RemoveHooks();
        game::Bridge::Shutdown();

        logger_->info("d2bsng shutdown complete");
        spdlog::shutdown();
    } catch (const std::exception& ex) {
        logger_->error("Host::Shutdown failed: {}", ex.what());
        spdlog::shutdown();
    } catch (...) {
        logger_->error("Host::Shutdown failed with unknown exception");
        spdlog::shutdown();
    }
}

void Host::SetupPaths(HMODULE hModule) {
    // Resolve DLL directory from the module handle.
    std::array<wchar_t, MAX_PATH> dllPath{};
    GetModuleFileNameW(hModule, dllPath.data(), MAX_PATH);
    auto basePath = std::filesystem::path(dllPath.data()).parent_path();
    dllDir_ = basePath;

    // Seed AppConfig.scriptPaths.basePath before LoadConfig() so GetPathRelScript()
    // and INI resolution have a valid base. LoadSettings() overwrites all four
    // ScriptPaths fields from the [settings] section immediately after.
    auto& appConfig = config::GetAppConfig();
    config::ScriptPaths paths;
    paths.basePath = basePath;
    appConfig.SetScriptPaths(std::move(paths));
}

namespace {

constexpr std::string_view INI_FILE_NAME = "d2bs.ini";

// A farm leaves the log to grow unattended, so the file is capped and rolled
// rather than trusted to stay small.
constexpr size_t MAX_LOG_SIZE = 10 * 1024 * 1024;
constexpr size_t MAX_LOG_FILES = 3;

// A profile name is user text that reaches a path here.
std::string SanitizeForFileName(std::string_view name) {
    std::string out;
    for (const char c : name) {
        const bool safe = std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '-' || c == '_' || c == '.';
        out.push_back(safe ? c : '_');
    }
    return out;
}

// Per-profile, so concurrent instances never write to the same file.
std::string LogFileName() {
    const auto profile = SanitizeForFileName(game::GetLaunchProfile().value_or(std::string{}));
    return profile.empty() ? "d2bs.log" : "d2bs-" + profile + ".log";
}

}  // namespace

void Host::SetupLogging() {
    // Two sinks: the ConsoleSink fans every frontend-internal entry out to
    // game::console::OnMessage with source=Log, and the file is what outlives
    // the process - the console's ring buffer dies with it. A file that cannot
    // be opened is not fatal; the console still has everything.
    // Both go to the fan-out sink every named logger already shares, so the
    // loggers the backend created during Bridge::Init - which runs at DLL
    // attach, before this - start writing here too rather than staying
    // attached to whatever the default logger was at the time.
    utils::AddLogSink(std::make_shared<runtime::console::ConsoleSink>());

    std::string logFile;
    std::string openError;
    try {
        const auto logPath = ConfigPath().parent_path() / "logs" / LogFileName();
        std::filesystem::create_directories(logPath.parent_path());
        logFile = logPath.string();
        utils::AddLogSink(std::make_shared<spdlog::sinks::rotating_file_sink_mt>(logFile, MAX_LOG_SIZE, MAX_LOG_FILES));
    } catch (const std::exception& ex) {
        openError = ex.what();
    }

    logger_ = utils::GetLogger("d2bs");
    logger_->set_level(spdlog::level::debug);

    spdlog::set_default_logger(logger_);
    // Loggers only flush themselves from warn, so this is what gets everything
    // below that onto disk. Without it a crash loses whatever the file buffer
    // was still holding, which is exactly the run-up to the crash.
    spdlog::flush_every(std::chrono::seconds{1});

    if (openError.empty()) {
        logger_->info("logging to {}", logFile);
    } else {
        logger_->error("no file log: {}", openError);
    }
}

std::filesystem::path Host::ConfigPath() {
    return dllDir_ / INI_FILE_NAME;
}

void Host::LoadConfig() {
    auto& appConfig = config::GetAppConfig();
    appConfig.store = std::make_unique<config::IniConfigStore>(ConfigPath());
    appConfig.store->LoadSettings(appConfig);
}

namespace {

// Runs a game-thread hook inside the game loop's "event hooks" phase for the Profiling panel.
template <auto Fn, typename... Args>
auto EventHook(Args... args) {
    const auto phase = gameloop::GameLoop::Instance().InPhase(gameloop::FramePhase::Events);
    return Fn(args...);
}

}  // namespace

game::GameCallbacks Host::BuildCallbacks() {
    using gameloop::FramePhase;
    using gameloop::GameLoop;

    game::GameCallbacks callbacks;

    // --- Input (blockable) ---
    callbacks.onKeyEvent = &EventHook<&KeyDownUpEventDispatch>;

    callbacks.onMouseClick = +[](game::ClickButton button, game::Position pos, game::KeyState state) -> bool {
        const auto phase = GameLoop::Instance().InPhase(FramePhase::Events);
        bool blocked = runtime::drawing::Drawable::OnClick(button, pos.ToPoint(), game::GetGameState());
        MouseClickEventDispatch(button, pos, state);
        return blocked;
    };

    callbacks.onMouseMove = +[](game::Position pos) {
        const auto phase = GameLoop::Instance().InPhase(FramePhase::Events);
        runtime::drawing::Drawable::OnMouseMove(pos.ToPoint(), game::GetGameState());
        MouseMoveEventDispatch(pos);
    };

    // --- Chat (blockable) ---
    callbacks.onChatMessage = &EventHook<&ChatEventDispatch>;
    callbacks.onChatInput = &EventHook<&ChatInputEventDispatch>;
    callbacks.onWhisper = &EventHook<&WhisperEventDispatch>;

    // Overlay/terminal Enter -> RunCommand dispatch. Fire-and-forget.
    callbacks.onConsoleInput = &runtime::script::RunCommand;

    // Console output sink + per-frame UI render: lets the port console host feed
    // and draw the frontend console without a direct dependency on it.
    callbacks.onConsoleMessage = &runtime::console::OnMessage;
    callbacks.onConsoleDrawFrame = &runtime::console::DrawFrame;

    // --- Packets (blockable) ---
    callbacks.onGamePacketReceived = &EventHook<&GamePacketEventDispatch>;
    callbacks.onGamePacketSent = &EventHook<&GamePacketSentEventDispatch>;
    callbacks.onRealmPacket = &EventHook<&RealmPacketEventDispatch>;

    // --- Game lifecycle ---
    callbacks.onGameEvent = &EventHook<&GameActionEventDispatch>;
    callbacks.onItemAction = &EventHook<&ItemActionEventDispatch>;

    // Observed monster deaths feed the character-state kill counter. Runs on the
    // game thread (death packet hook) where the frame write lock is held, so the
    // Unit::Find inside RecordKill resolves lock-free (see game/GameLock.h).
    callbacks.onMonsterDeath = +[](uint32_t unitId) {
        const auto phase = GameLoop::Instance().InPhase(FramePhase::Events);
        services::characterstate::CharacterState::Instance().RecordKill(unitId);
    };

    // --- IPC ---
    // Reference D2Handlers.cpp:184-196 intercepts two reserved WM_COPYDATA
    // dwData values exclusively (no fall-through to CopyDataEvent):
    //   IpcMode::Evaluate      (0x1337)  - run payload as JS via ScriptEngine.
    //   IpcMode::SwitchProfile (0x31337) - set active profile via services::profile::Switch.
    callbacks.onIPC = +[](game::IpcMode mode, const std::string& payload) {
        const auto phase = GameLoop::Instance().InPhase(FramePhase::Events);
        switch (mode) {
            case game::IpcMode::Evaluate:
                ScriptEngine::Instance().Evaluate(payload);
                return;
            case game::IpcMode::SwitchProfile:
                if (services::profile::Switch(payload)) {
                    logger_->info("IPC profile switch: '{}'", payload);
                } else {
                    logger_->warn("IPC profile switch failed for '{}' (profile does not exist)", payload);
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
        GameLoop::Instance().OnSleep(duration);
    };
    callbacks.onDraw = +[]() {
        const auto phase = GameLoop::Instance().InPhase(FramePhase::Draw);
        GameLoop::Instance().OnDraw();
    };

    return callbacks;
}

}  // namespace d2bs::runtime
