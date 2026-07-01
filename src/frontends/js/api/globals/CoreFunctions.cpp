#include "CoreFunctions.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <memory>
#include <random>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

#include <sqlite3.h>

#include <Windows.h>

#include <WinInet.h>
#pragma comment(lib, "wininet.lib")

#include "api/classes/io/JSDirectory.h"
#include "api/classes/scripting/JSScript.h"
#include "api/core/V8Convert.h"
#include "api/core/V8Error.h"
#include "api/core/V8Extract.h"
#include "api/core/V8Function.h"
#include "components/dde/DdeService.h"
#include "components/events/DelayedEvent.h"
#include "components/events/EventDispatch.h"
#include "components/script/ScriptEngine.h"
#include "config/AppConfig.h"
#include "config/Version.h"
#include "game/Console.h"
#include "game/GameHelpers.h"
#include "speedhack/Speedhack.h"
#include "utils/threadutils.h"
#include "utils/utils.h"

namespace d2bs::api::globals {

using namespace d2bs::api;

// getPacket / sendPacket share packet-construction logic. This helper extracts
// either an ArrayBuffer/TypedArray or (byteSize, value) pairs into `out`.
static bool BuildPacketFromArgs(const v8::FunctionCallbackInfo<v8::Value>& args, std::vector<uint8_t>& out) {
    auto* isolate = args.GetIsolate();

    if (!config::GetAppConfig().enableUnsupported.load()) {
        v8_error::WarnAndReturnFalse(args, "Packet API requires EnableUnsupported = true in d2bs.ini");
        return false;
    }

    uint32_t len = 0;

    if (args.Length() >= 1 && args[0]->IsObject()) {
        v8::Local<v8::Object> obj = args[0].As<v8::Object>();
        std::shared_ptr<v8::BackingStore> backingStore;
        size_t byteOffset = 0;

        if (obj->IsArrayBuffer()) {
            backingStore = obj.As<v8::ArrayBuffer>()->GetBackingStore();
            len = backingStore->ByteLength();
        } else if (obj->IsTypedArray()) {
            auto typedArray = obj.As<v8::TypedArray>();
            backingStore = typedArray->Buffer()->GetBackingStore();
            len = typedArray->ByteLength();
            byteOffset = typedArray->ByteOffset();
        } else {
            v8_error::WarnAndReturnFalse(args, "invalid ArrayBuffer parameter");
            return false;
        }

        if (len > 0) {
            out.resize(len);
            const auto* start = static_cast<const uint8_t*>(backingStore->Data()) + byteOffset;
            std::memcpy(out.data(), start, len);
        }
    } else {
        if (args.Length() < 2 || args.Length() % 2 != 0) {
            v8_error::WarnAndReturnFalse(args, "invalid packet format");
            return false;
        }

        out.resize(args.Length() * 2);

        for (int32_t i = 0; i < args.Length(); i += 2) {
            uint32_t size = v8_convert::ToUint32(isolate, args[i]);
            uint32_t value = v8_convert::ToUint32(isolate, args[i + 1]);
            if (size != 1 && size != 2 && size != 4) {
                v8_error::ThrowError(isolate, "Invalid packet field size (must be 1, 2, or 4)");
                return false;
            }
            if (len + size > out.size()) {
                v8_error::ThrowError(isolate, "Packet buffer overflow");
                return false;
            }
            std::memcpy(&out[len], &value, size);
            len += size;
        }
        out.resize(len);
    }
    return true;
}

// Resolve an include path: looks in libs/ subdirectory only
static std::filesystem::path FindIncludePath(const std::string& relativePath) {
    auto resolved = config::GetPathRelScript("libs/" + relativePath);
    if (!resolved.empty() && std::filesystem::exists(resolved))
        return resolved;
    return {};
}

// Resolve a script path: direct path only (matching d2bs reference behavior)
static std::filesystem::path FindScriptPath(const std::string& relativePath) {
    auto resolved = config::GetPathRelScript(relativePath);
    if (!resolved.empty() && std::filesystem::exists(resolved))
        return resolved;
    return {};
}

// Shared implementation for stringToEUC and utf8ToEuc:
// Convert UTF-8 string to ANSI (system codepage) encoding.
static void ConvertToEuc(const v8::FunctionCallbackInfo<v8::Value>& args) {
    auto* isolate = args.GetIsolate();

    if (args.Length() == 0 || args[0]->IsNullOrUndefined()) {
        args.GetReturnValue().SetNull();
        return;
    }

    // Convert: UTF-8 -> wide string -> ANSI (system codepage) -> UTF-8 for V8
    std::string str = v8_convert::ToString(isolate, args[0]);
    std::wstring wide = utils::ToWStr(str);
    std::string ansi = utils::ToStr(wide, CP_ACP);
    args.GetReturnValue().Set(v8_convert::ToV8(isolate, ansi));
}

void RegisterCoreFunctions(v8::Isolate* isolate, v8::Local<v8::ObjectTemplate> global) {
    /// @description Prints each argument to the console as its own Info-level message.
    /// @signature print(...args: any)
    /// @param args {any} - zero or more values; each is stringified and emitted separately
    /// @returns {null}
    v8_function::Register(
        isolate, global, "print", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
            auto* isolate = args.GetIsolate();

            // Identify the originating script/isolate by short name - same
            // value the per-script logger is named after; cheaper than going
            // through the logger when we're not actually logging via spdlog.
            auto* script = ScriptEngine::Instance().GetScript(isolate);
            std::string name = script ? script->GetName() : "js";
            const auto source = (script != nullptr && script->GetMode() == ScriptMode::Console)
                                    ? game::console::MessageSource::ConsolePrint
                                    : game::console::MessageSource::Print;

            // Unusual but deliberate: emit one message per argument rather than
            // joining them with a space. Matches reference/d2bs my_print
            // (JSCore.cpp:49) -
            // scripts that rely on per-arg log lines would otherwise break.
            for (int32_t i = 0; i < args.Length(); ++i) {
                if (!args[i]->IsNullOrUndefined()) {
                    game::console::OnMessage({
                        .source = source,
                        .name = name,
                        .level = game::console::MessageLevel::Info,
                        .text = v8_convert::ToString(isolate, args[i]),
                    });
                }
            }

            args.GetReturnValue().SetNull();
        });

    /// @description Pauses the calling script for the given milliseconds while still processing its events.
    /// @signature delay(ms: number)
    /// @param ms {number} - milliseconds to wait; clamped to a minimum of 1
    /// @returns {undefined}
    v8_function::Register(
        isolate, global, "delay", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
            if (!v8_error::CheckArgCount(args, 1, "delay")) {
                return;
            }
            auto* isolate = args.GetIsolate();

            // Clamp to at least 1ms so events are always processed
            // ToUint32 follows ECMAScript ToUint32: NaN/undefined/arrays -> 0
            uint32_t ms = std::max(v8_convert::ToUint32(isolate, args[0]), 1U);
            auto* script = ScriptEngine::Instance().GetScript(isolate);
            if (script) {
                // Snapshot the JS stack at this yield only when the console's
                // Stacktraces panel has this script selected (mode != Off) -
                // otherwise it's a full stack walk on every delay(). On the
                // script's own thread here, so it's synchronous, no interrupt.
                if (script->GetStackCaptureMode() != StackCaptureMode::Off) {
                    script->RefreshLastStackTrace();
                }
                script->ExecuteEvents(std::chrono::milliseconds(ms));
            } else {
                std::this_thread::sleep_for(std::chrono::milliseconds(ms));
            }
        });

    /// @description Loads and runs a libs/ script file into the current script's scope, once.
    /// @signature include(file: string)
    /// @param file {string} - path relative to the libs/ subdirectory
    /// @returns {boolean} - true on success; false if the file is not found under libs/ or there is no current script
    v8_function::Register(
        isolate, global, "include", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
            auto* isolate = args.GetIsolate();
            args.GetReturnValue().SetFalse();

            if (!v8_error::CheckArgCount(args, 1, "include")) {
                return;
            }

            if (!v8_error::CheckIsString(args, 0, "file")) {
                return;
            }

            std::string file = v8_convert::ToString(isolate, args[0]);
            auto absPath = FindIncludePath(file);
            if (absPath.empty()) {
                return;
            }

            auto* script = ScriptEngine::Instance().GetScript(isolate);
            if (!script) {
                return;
            }

            args.GetReturnValue().Set(script->Include(absPath));
        });

    /// @description Reports whether a libs/ file has already been included by the current script.
    /// @signature isIncluded(file: string)
    /// @param file {string} - path relative to the libs/ subdirectory
    /// @returns {boolean} - true if the file is already included; false otherwise
    v8_function::Register(
        isolate, global, "isIncluded", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
            auto* isolate = args.GetIsolate();
            args.GetReturnValue().SetFalse();

            if (!v8_error::CheckArgCount(args, 1, "isIncluded")) {
                return;
            }

            if (!v8_error::CheckIsString(args, 0, "file")) {
                return;
            }

            std::string file = v8_convert::ToString(isolate, args[0]);
            auto absPath = FindIncludePath(file);
            if (absPath.empty()) {
                return;
            }

            auto* script = ScriptEngine::Instance().GetScript(isolate);
            if (!script) {
                return;
            }

            args.GetReturnValue().Set(script->IsIncluded(absPath));
        });

    /// @description Starts a new independent script on its own thread, forwarding any extra arguments to it.
    /// @signature load(file: string, ...args: any)
    /// @param file {string} - script path relative to the script base path
    /// @param args {any} - optional serializable values forwarded to the new script
    /// @returns {D2BSScript|boolean|null} - the started script on success; false if the file is not found or there is
    /// no
    ///                                  current script; null if the script failed to start
    /// @throws {Error} - if an extra argument cannot be serialized
    v8_function::Register(
        isolate, global, "load", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
            auto* isolate = args.GetIsolate();
            args.GetReturnValue().SetFalse();

            if (args.Length() < 1) {
                v8_error::ThrowError(isolate, "load requires at least 1 argument");
                return;
            }

            if (!v8_error::CheckIsString(args, 0, "file")) {
                return;
            }

            std::string file = v8_convert::ToString(isolate, args[0]);

            auto absPath = FindScriptPath(file);
            if (absPath.empty()) {
                GetLogger(isolate)->warn("load: could not find file \"{}\"", file);
                return;
            }

            auto* currentScript = ScriptEngine::Instance().GetScript(isolate);
            if (!currentScript) {
                return;
            }

            auto mode = currentScript->GetMode();
            if (mode == ScriptMode::Console) {
                mode = (game::GetGameState() == game::GameState::InGame) ? ScriptMode::InGame : ScriptMode::OutOfGame;
            }

            std::vector<std::vector<uint8_t>> serializedArgs;
            for (int32_t i = 1; i < args.Length(); i++) {
                v8::ValueSerializer serializer(isolate);
                serializer.WriteHeader();
                if (!serializer.WriteValue(isolate->GetCurrentContext(), args[i]).FromMaybe(false)) {
                    v8_error::ThrowError(isolate, "Failed to serialize argument for load()");
                    return;
                }
                auto [data, size] = serializer.Release();
                serializedArgs.emplace_back(data, data + size);
                std::free(data);  // NOLINT(cppcoreguidelines-no-malloc) - V8's ValueSerializer allocates with realloc()
            }

            auto newScript = ScriptEngine::Instance().StartScript(absPath, mode, std::move(serializedArgs));
            if (newScript) {
                auto scriptObj = classes::JSScript::Create(isolate, newScript.get());
                args.GetReturnValue().Set(scriptObj);
            } else {
                args.GetReturnValue().SetNull();
            }
        });

    /// @description Stops the current script or all scripts, terminating execution immediately.
    /// @signature stop(current?: number | boolean)
    /// @param current {number|boolean} - the number 1 or true stops only the current script; otherwise all scripts
    /// @returns {undefined}
    v8_function::Register(
        isolate, global, "stop", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
            auto* isolate = args.GetIsolate();

            bool stopCurrent = false;
            if (args.Length() > 0) {
                if (args[0]->IsNumber()) {
                    stopCurrent = v8_convert::ToInt32(isolate, args[0]) == 1;
                } else if (args[0]->IsBoolean()) {
                    stopCurrent = v8_convert::ToBool(isolate, args[0]);
                }
            }

            if (stopCurrent) {
                auto* script = ScriptEngine::Instance().GetScript(isolate);
                if (script) {
                    script->Stop();
                    // TerminateExecution ensures code after stop() does not run (reference returns JS_FALSE for same
                    // effect).
                    isolate->TerminateExecution();
                }
            } else {
                ScriptEngine::Instance().StopAllScripts();
                isolate->TerminateExecution();
            }
        });

    /// @description Returns a monotonic millisecond timestamp for timing/elapsed measurements.
    /// @signature getTickCount()
    /// @returns {number} - a monotonically increasing millisecond counter; only differences are meaningful (not
    ///                     wall-clock time)
    v8_function::Register(
        isolate, global, "getTickCount", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
            auto* isolate = args.GetIsolate();

            // NOTE: reference uses GetTickCount(), we use std::chrono
            auto elapsed = std::chrono::steady_clock::now().time_since_epoch();
            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
            args.GetReturnValue().Set(v8_convert::ToV8(isolate, static_cast<double>(ms)));
        });

    /// @description Sets the speedhack time multiplier affecting the game's perceived clock speed.
    /// @signature setSpeed(multiplier: number)
    /// @param multiplier {number} - speed multiplier (e.g. 1.0 = normal, 2.0 = double)
    /// @returns {undefined}
    v8_function::Register(
        isolate, global, "setSpeed", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
            if (!v8_error::CheckArgCount(args, 1, "setSpeed")) {
                return;
            }
            const auto multiplier = static_cast<float>(v8_convert::ToDouble(args.GetIsolate(), args[0]));
            speedhack::SetSpeed(multiplier);
        });

    /// @description Returns the current speedhack time multiplier.
    /// @signature getSpeed()
    /// @returns {number} - the current speed multiplier
    v8_function::Register(
        isolate, global, "getSpeed", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
            args.GetReturnValue().Set(v8_convert::ToV8(args.GetIsolate(), speedhack::GetSpeed()));
        });

    /// @description Returns a pseudo-random integer. Uniform over [low, high] only when high >= low + 2; when high <=
    ///              low + 1 (including the adjacent high == low + 1 case) it returns high unchanged, so low is never
    ///              produced in that case.
    /// @signature rand(low: number, high: number)
    /// @param low {number} - lower bound (only reachable when high >= low + 2)
    /// @param high {number} - upper bound
    /// @returns {number} - a random integer in [low, high] when high >= low + 2, otherwise high; 0 if args are invalid
    v8_function::Register(
        isolate, global, "rand", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
            auto* isolate = args.GetIsolate();

            if (args.Length() < 2 || !args[0]->IsNumber() || !args[1]->IsNumber()) {
                args.GetReturnValue().Set(0);
                return;
            }

            // Thread-safe RNG, seeded once per thread
            static thread_local std::mt19937 rng{std::random_device{}()};

            int32_t low = v8_convert::ToInt32(isolate, args[0]);
            int32_t high = v8_convert::ToInt32(isolate, args[1]);

            if (high > low + 1) {
                std::uniform_int_distribution dist(low, high);
                args.GetReturnValue().Set(dist(rng));
            } else {
                args.GetReturnValue().Set(high);
            }
        });

    /// @description Returns the D2BS version string, or prints it to the console when given any argument.
    /// @signature version()
    /// @returns {string} - the version string
    /// @signature version(any: any)
    /// @param any {any} - presence (value ignored) switches to print-to-console mode
    /// @returns {boolean} - true after printing
    v8_function::Register(
        isolate, global, "version", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
            auto* isolate = args.GetIsolate();

            if (args.Length() < 1) {
                // Return version string
                args.GetReturnValue().Set(v8_convert::ToV8(isolate, D2BS_VERSION));
                return;
            }

            // Print version with D2 color codes matching reference:
            // \xff c4=gold, c1=red, c3=blue
            std::string versionMsg = "\xff"
                                     "c4D2BS\xff"
                                     "c1 \xff"
                                     "c3" +
                                     std::string(D2BS_VERSION);
            auto* script = ScriptEngine::Instance().GetScript(isolate);
            const auto source = (script != nullptr && script->GetMode() == ScriptMode::Console)
                                    ? game::console::MessageSource::ConsolePrint
                                    : game::console::MessageSource::Print;
            game::console::OnMessage({
                .source = source,
                .name = "d2bsng",
                .level = game::console::MessageLevel::Info,
                .text = versionMsg,
            });

            args.GetReturnValue().Set(true);
        });

    /// @description No-op kept for API compatibility (strict mode is decided per-script by the V8 parser).
    /// @signature js_strict()
    /// @returns {boolean} - true
    /// @signature js_strict(value: any)
    /// @param value {any} - ignored
    /// @returns {undefined}
    v8_function::Register(
        isolate, global, "js_strict", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
            if (args.Length() == 0) {
                args.GetReturnValue().Set(true);
            }
        });

    /// @description Logs the current JavaScript call stack plus the native thread stack at Info level.
    /// @signature stacktrace()
    /// @returns {boolean} - always true
    v8_function::Register(
        isolate, global, "stacktrace", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
            auto* isolate = args.GetIsolate();

            v8::Local<v8::StackTrace> stackTrace =
                v8::StackTrace::CurrentStackTrace(isolate, 50, v8::StackTrace::kDetailed);

            auto logger = GetLogger(isolate);
            logger->info("JavaScript Stack Trace:");

            for (int32_t i = 0; i < stackTrace->GetFrameCount(); ++i) {
                v8::Local<v8::StackFrame> frame = stackTrace->GetFrame(isolate, i);
                std::string functionName = v8_convert::ToString(isolate, frame->GetFunctionName());
                std::string scriptName = v8_convert::ToString(isolate, frame->GetScriptName());
                int32_t lineNumber = frame->GetLineNumber();
                int32_t column = frame->GetColumn();

                if (functionName.empty()) {
                    functionName = "<anonymous>";
                }
                if (scriptName.empty()) {
                    scriptName = "<unknown>";
                }

                logger->info("  at {} ({}:{}:{})", functionName, scriptName, lineNumber, column);
            }

            logger->info("Native stack trace:\n{}", thread_utils::GetThreadStacktrace());

            args.GetReturnValue().Set(true);
        });

    /// @description Prints each argument to the console as its own Debug-level message.
    /// @signature debugLog(...args: any)
    /// @param args {any} - zero or more values; each is stringified and emitted separately
    /// @returns {undefined}
    v8_function::Register(
        isolate, global, "debugLog", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
            auto* isolate = args.GetIsolate();
            auto* script = ScriptEngine::Instance().GetScript(isolate);
            std::string name = script ? script->GetName() : "js";
            const auto source = (script != nullptr && script->GetMode() == ScriptMode::Console)
                                    ? game::console::MessageSource::ConsolePrint
                                    : game::console::MessageSource::Print;

            // One message per argument, not space-joined - matches reference/d2bs
            // my_debugLog (JSCore.cpp:307). See the print() binding above for
            // the same rationale.
            for (int32_t i = 0; i < args.Length(); ++i) {
                if (!args[i]->IsNullOrUndefined()) {
                    game::console::OnMessage({
                        .source = source,
                        .name = name,
                        .level = game::console::MessageLevel::Debug,
                        .text = v8_convert::ToString(isolate, args[i]),
                    });
                }
            }
        });

    /// @description Returns the current thread's handle (GetCurrentThread), despite the name.
    /// @signature getThreadPriority()
    /// @returns {number} - the current thread handle as a numeric value
    v8_function::Register(
        isolate, global, "getThreadPriority", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
            // Handles are pointer-sized: use double (exact up to 2^53) to avoid truncation on x64.
            args.GetReturnValue().Set(static_cast<double>(reinterpret_cast<uintptr_t>(GetCurrentThread())));
        });

    /// @description Converts a UTF-8 string to the system ANSI codepage (CP_ACP) encoding.
    /// @signature stringToEUC(text: string)
    /// @param text {string} - the input text
    /// @returns {string|null} - the ANSI-encoded string, or null if input is missing/null/undefined
    v8_function::Register(
        isolate, global, "stringToEUC", +[](const v8::FunctionCallbackInfo<v8::Value>& args) { ConvertToEuc(args); });

    /// @description Performs a DDE (Dynamic Data Exchange) transaction; mode 0/1/2 selects the type.
    /// @signature sendDDE(mode: number, server: string, topic: string, item: string, data: string)
    /// @param mode {number} - transaction type 0/1/2 (Request retrieves data)
    /// @param server {string} - DDE server/service name
    /// @param topic {string} - DDE topic
    /// @param item {string} - DDE item
    /// @param data {string} - DDE data payload
    /// @returns {string|undefined} - the response payload for a successful Request; undefined otherwise
    v8_function::Register(
        isolate, global, "sendDDE", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
            auto* isolate = args.GetIsolate();

            if (args.Length() < 5) {
                v8_error::ThrowError(isolate, "sendDDE requires 5 arguments");
                return;
            }

            uint32_t mode = v8_convert::ToUint32(isolate, args[0]);
            std::string server = v8_convert::ToString(isolate, args[1]);
            std::string topic = v8_convert::ToString(isolate, args[2]);
            std::string item = v8_convert::ToString(isolate, args[3]);
            std::string data = v8_convert::ToString(isolate, args[4]);

            // JS `mode` 0/1/2 maps 1:1 to the Transaction enum values.
            if (mode > static_cast<uint32_t>(dde::Transaction::Evaluate)) {
                return;
            }
            auto txn = static_cast<dde::Transaction>(mode);

            // Matches reference/d2bs JSCore.cpp my_sendDDE: never throws on DDE failure; any failure
            // is logged and the JS return value stays undefined (caller sees no response). Only
            // Request sets a return value, and only on successful payload retrieval.
            auto result = dde::DdeService::Instance().Send(txn, server, topic, item, data);
            if (txn == dde::Transaction::Request && result) {
                args.GetReturnValue().Set(v8_convert::ToV8(isolate, *result));
            }
        });

    /// @description Broadcasts a message to all other scripts, firing their "scriptmsg" listeners.
    /// @signature scriptBroadcast(...args: any)
    /// @param args {any} - one or more values forwarded to other scripts' listeners
    /// @returns {null}
    v8_function::Register(
        isolate, global, "scriptBroadcast", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
            auto* isolate = args.GetIsolate();

            if (args.Length() < 1) {
                v8_error::ThrowError(isolate, "You must specify something to broadcast");
                return;
            }

            ScriptBroadcastEventDispatch(args);
            args.GetReturnValue().SetNull();
        });

    /// @description Shows the d2bs developer console window.
    /// @signature showConsole()
    /// @returns {null}
    v8_function::Register(
        isolate, global, "showConsole", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
            game::console::Show();
            args.GetReturnValue().SetNull();
        });

    /// @description Hides the d2bs developer console window.
    /// @signature hideConsole()
    /// @returns {null}
    v8_function::Register(
        isolate, global, "hideConsole", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
            game::console::Hide();
            args.GetReturnValue().SetNull();
        });

    /// @description Returns the game window handle (HWND) as a number.
    /// @signature handler()
    /// @returns {number} - the game window handle as a numeric value
    v8_function::Register(
        isolate, global, "handler", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
            auto hwnd = reinterpret_cast<uintptr_t>(game::GetHwnd());
            args.GetReturnValue().Set(static_cast<double>(hwnd));
        });

    /// @description Loads an MPQ archive from the given path (skipped if the path fails validation).
    /// @signature loadMpq(path: string)
    /// @param path {string} - path to the MPQ file
    /// @returns {null}
    v8_function::Register(
        isolate, global, "loadMpq", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
            args.GetReturnValue().SetNull();
            auto* isolate = args.GetIsolate();

            if (!v8_error::CheckArgCount(args, 1, "loadMpq")) {
                return;
            }

            std::string path = v8_convert::ToString(isolate, args[0]);
            if (config::IsValidPath(path)) {
                game::LoadMpq(path);
            }
        });

    /// @description Injects a received game packet into the client (requires EnableUnsupported in d2bs.ini).
    /// @signature getPacket(buffer: ArrayBuffer | TypedArray)
    /// @param buffer {ArrayBuffer|TypedArray} - raw packet bytes as a single object argument
    /// @signature getPacket(...sizeValuePairs: number)
    /// @param sizeValuePairs {number} - alternating size (1|2|4) and value entries; count must be even and >= 2
    /// @returns {boolean} - true on success; false if the packet API is disabled or the argument shape is invalid (a
    /// bad
    ///                      buffer object, or a pair count that is < 2 or odd)
    /// @throws {Error} - if a packet field size is not 1, 2, or 4, or the packet buffer overflows
    v8_function::Register(
        isolate, global, "getPacket", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
            std::vector<uint8_t> packet;
            if (!BuildPacketFromArgs(args, packet)) {
                return;
            }
            if (!packet.empty()) {
                game::ReceiveGamePacket(packet);
            }
            args.GetReturnValue().Set(true);
        });

    /// @description Sends a game packet to the server (requires EnableUnsupported in d2bs.ini).
    /// @signature sendPacket(buffer: ArrayBuffer | TypedArray)
    /// @param buffer {ArrayBuffer|TypedArray} - raw packet bytes as a single object argument
    /// @signature sendPacket(...sizeValuePairs: number)
    /// @param sizeValuePairs {number} - alternating size (1|2|4) and value entries; count must be even and >= 2
    /// @returns {boolean} - true on success; false if the packet API is disabled or the argument shape is invalid (a
    /// bad
    ///                      buffer object, or a pair count that is < 2 or odd)
    /// @throws {Error} - if a packet field size is not 1, 2, or 4, or the packet buffer overflows
    v8_function::Register(
        isolate, global, "sendPacket", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
            std::vector<uint8_t> packet;
            if (!BuildPacketFromArgs(args, packet)) {
                return;
            }
            if (!packet.empty()) {
                game::SendGamePacket(packet);
            }
            args.GetReturnValue().Set(true);
        });

    /// @description Returns the machine's public IP via a blocking HTTP request to api.ipify.org.
    /// @signature getIP()
    /// @returns {string} - the public IP address, or an empty string on failure
    v8_function::Register(
        isolate, global, "getIP", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
            auto* isolate = args.GetIsolate();

            HINTERNET hInternet = InternetOpenA("d2bs", INTERNET_OPEN_TYPE_PRECONFIG, nullptr, nullptr, 0);
            if (!hInternet) {
                args.GetReturnValue().Set(v8_convert::ToV8(isolate, ""));
                return;
            }

            HINTERNET hUrl = InternetOpenUrlA(hInternet, "http://api.ipify.org", nullptr, 0, INTERNET_FLAG_RELOAD, 0);
            if (!hUrl) {
                InternetCloseHandle(hInternet);
                args.GetReturnValue().Set(v8_convert::ToV8(isolate, ""));
                return;
            }

            std::array<char, 64> buffer = {};
            DWORD bytesRead = 0;
            InternetReadFile(hUrl, buffer.data(), static_cast<DWORD>(buffer.size()) - 1, &bytesRead);
            buffer.at(bytesRead) = '\0';

            InternetCloseHandle(hUrl);
            InternetCloseHandle(hInternet);

            args.GetReturnValue().Set(v8_convert::ToV8(isolate, std::string(buffer.data())));
        });

    /// @description Sends a low-level mouse click to the game window at the given screen coordinates.
    /// @signature sendClick(x: number, y: number)
    /// @param x {number} - screen x coordinate
    /// @param y {number} - screen y coordinate
    /// @returns {null}
    v8_function::Register(
        isolate, global, "sendClick", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
            if (!v8_error::CheckArgCount(args, 2, "sendClick")) {
                return;
            }

            auto p = v8_extract::Point(args, 0).value_or(game::Point::Zero);
            // Port owns the full sequence - timing, mouse-down/up ordering,
            // and any port-specific ceremony (see src/<port>/game for impl).
            game::SendClick(p);
            args.GetReturnValue().SetNull();
        });

    /// @description Sends a low-level keypress (virtual key code) to the game window.
    /// @signature sendKey(key: number)
    /// @param key {number} - virtual key code
    /// @returns {null}
    v8_function::Register(
        isolate, global, "sendKey", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
            auto* isolate = args.GetIsolate();

            if (!v8_error::CheckArgCount(args, 1, "sendKey")) {
                return;
            }

            uint32_t key = v8_convert::ToUint32(isolate, args[0]);
            // Port owns the full sequence - key-down/up pair, any prompt
            // juggling, and any port-specific timing (see src/<port>/game).
            game::SendKey(key);
            args.GetReturnValue().SetNull();
        });

    /// @description Converts a UTF-8 string to the system ANSI codepage (CP_ACP) encoding (same as stringToEUC).
    /// @signature utf8ToEuc(text: string)
    /// @param text {string} - the input text
    /// @returns {string|null} - the ANSI-encoded string, or null if input is missing/null/undefined
    v8_function::Register(
        isolate, global, "utf8ToEuc", +[](const v8::FunctionCallbackInfo<v8::Value>& args) { ConvertToEuc(args); });

    /// @description Returns the SQLite library version string.
    /// @signature sqlite_version()
    /// @returns {string} - the SQLite version string (e.g. "3.x.y")
    v8_function::Register(
        isolate, global, "sqlite_version", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
            auto* isolate = args.GetIsolate();
            args.GetReturnValue().Set(v8_convert::ToV8(isolate, sqlite3_libversion()));
        });

    /// @description Returns the number of bytes of memory currently in use by SQLite.
    /// @signature sqlite_memusage()
    /// @returns {number} - bytes of memory currently allocated by SQLite
    v8_function::Register(
        isolate, global, "sqlite_memusage", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
            auto* isolate = args.GetIsolate();
            args.GetReturnValue().Set(v8_convert::ToV8(isolate, static_cast<double>(sqlite3_memory_used())));
        });

    /// @description Opens (creating if needed) a directory relative to the script base path.
    /// @signature dopen(path: string)
    /// @param path {string} - directory path relative to the script base path
    /// @returns {Folder} - a Folder handle for the opened/created directory
    /// @throws {Error} - if the path is invalid, or the directory cannot be created (e.g. parent does not exist)
    v8_function::Register(
        isolate, global, "dopen", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
            auto* isolate = args.GetIsolate();
            auto context = isolate->GetCurrentContext();

            if (!v8_error::CheckArgCount(args, 1, "dopen")) {
                return;
            }

            if (!args[0]->IsString()) {
                v8_error::ThrowTypeError(isolate, "dopen() requires a string path argument");
                return;
            }

            std::string path = v8_convert::ToString(isolate, args[0]);

            auto fullPath = config::GetPathRelScript(path);
            if (fullPath.empty()) {
                auto msg = "Invalid path: " + path;
                v8_error::ThrowError(isolate, msg);
                return;
            }

            // Attempt to create the directory (like _wmkdir in reference).
            // create_directory fails if parent doesn't exist, matching reference ENOENT behavior.
            std::error_code ec;
            std::filesystem::create_directory(fullPath, ec);
            if (ec) {
                auto msg = "Couldn't get directory " + path + ", path '" + fullPath.string() + "' not found";
                v8_error::ThrowError(isolate, msg);
                return;
            }

            auto data = std::make_unique<classes::DirectoryData>(std::filesystem::path(path));
            auto dirObj = classes::JSDirectory::CreateInstance(isolate, context, std::move(data));
            if (dirObj.IsEmpty())
                return;
            args.GetReturnValue().Set(dirObj);
        });

    /// @description Looks up a single running Script by the given selector.
    /// @signature getScript()
    /// @signature getScript(current: boolean)
    /// @param current {boolean} - true selects the current script; false selects the first script
    /// @signature getScript(threadId: number)
    /// @param threadId {number} - native thread ID to match
    /// @signature getScript(path: string)
    /// @param path {string} - script path/name to match (slashes normalized to backslashes)
    /// @returns {D2BSScript|null} - the selected script, or null if none is found
    v8_function::Register(
        isolate, global, "getScript", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
            auto* isolate = args.GetIsolate();
            auto& engine = ScriptEngine::Instance();

            args.GetReturnValue().SetNull();

            std::shared_ptr<Script> targetScript;

            if (args.Length() == 0) {
                // No arguments: return first script
                auto scripts = engine.GetAllScripts();
                if (!scripts.empty()) {
                    targetScript = scripts[0];
                }
            } else if (args[0]->IsBoolean()) {
                if (args[0]->BooleanValue(isolate)) {
                    // true: return current script (same-thread, wrap in shared_ptr is not needed)
                    auto* current = engine.GetScript(isolate);
                    if (current) {
                        auto scriptObj = classes::JSScript::Create(isolate, current);
                        if (!scriptObj.IsEmpty()) {
                            args.GetReturnValue().Set(scriptObj);
                        }
                    }
                    return;
                }
                // false: return first script in list
                auto scripts = engine.GetAllScripts();
                if (!scripts.empty()) {
                    targetScript = scripts.front();
                }
            } else if (args[0]->IsNumber()) {
                // Number: find by native thread ID
                auto targetId = v8_convert::ToUint32(isolate, args[0]);
                for (auto& script : engine.GetAllScripts()) {
                    if (script->GetNativeThreadId() == targetId) {
                        targetScript = script;
                        break;
                    }
                }
            } else if (args[0]->IsString()) {
                // String: find by path/name (convert / to \ for Windows)
                std::string pathStr = v8_convert::ToString(isolate, args[0]);
                std::ranges::replace(pathStr, '/', '\\');
                targetScript = engine.GetScriptByPath(std::filesystem::path(pathStr));
            }

            if (targetScript) {
                auto scriptObj = classes::JSScript::Create(isolate, targetScript.get());
                if (!scriptObj.IsEmpty()) {
                    args.GetReturnValue().Set(scriptObj);
                }
            }
        });

    /// @description Returns an array of Script objects, one for every currently running script.
    /// @signature getScripts()
    /// @returns {D2BSScript[]} - array of all running script objects
    v8_function::Register(
        isolate, global, "getScripts", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
            auto* isolate = args.GetIsolate();
            auto context = isolate->GetCurrentContext();
            auto& engine = ScriptEngine::Instance();

            auto scripts = engine.GetAllScripts();
            auto array = v8::Array::New(isolate, static_cast<int32_t>(scripts.size()));

            uint32_t index = 0;
            for (auto& script : scripts) {
                auto scriptObj = classes::JSScript::Create(isolate, script.get());
                if (!scriptObj.IsEmpty()) {
                    array->Set(context, index++, scriptObj).Check();
                }
            }

            args.GetReturnValue().Set(array);
        });

    /// @description Schedules a callback to run once after a delay on the calling script.
    /// @signature setTimeout(callback: function, ms: number)
    /// @param callback {function} - the function to invoke after the delay
    /// @callback callback() - invoked with no arguments after the delay; return value ignored
    /// @param ms {number} - delay in milliseconds before the callback runs
    /// @returns {number|null} - the event ID for use with clearTimeout, or null on error/no script
    v8_function::Register(
        isolate, global, "setTimeout", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
            auto* isolate = args.GetIsolate();
            args.GetReturnValue().SetNull();

            if (args.Length() < 2 || !args[0]->IsFunction() || !args[1]->IsNumber()) {
                v8_error::ReportError(args, "invalid params passed to setTimeout");
                return;
            }

            auto* script = ScriptEngine::Instance().GetScript(isolate);
            if (!script)
                return;

            auto fn = args[0].As<v8::Function>();
            uint32_t delayMs = v8_convert::ToUint32(isolate, args[1]);

            auto event = std::make_shared<DelayedEvent>(v8::Global<v8::Function>(isolate, fn));
            script->AddDelayedEvent(event);
            script->PostEvent(event, delayMs);
            args.GetReturnValue().Set(event->EventId());
        });

    /// @description Cancels a pending timeout created by setTimeout, by its event ID.
    /// @signature clearTimeout(id: number)
    /// @param id {number} - the event ID returned by setTimeout
    /// @returns {null}
    v8_function::Register(
        isolate, global, "clearTimeout", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
            args.GetReturnValue().SetNull();
            auto* isolate = args.GetIsolate();

            if (args.Length() < 1 || !args[0]->IsNumber()) {
                v8_error::ReportError(args, "invalid params passed to clearTimeout");
                return;
            }

            auto* script = ScriptEngine::Instance().GetScript(isolate);
            if (!script)
                return;

            uint32_t id = v8_convert::ToUint32(isolate, args[0]);
            script->RemoveDelayedEvent(id);
        });

    /// @description Schedules a callback to run repeatedly at a fixed interval on the calling script.
    /// @signature setInterval(callback: function, ms: number)
    /// @param callback {function} - the function to invoke on each interval
    /// @callback callback() - invoked with no arguments on each interval; return value ignored
    /// @param ms {number} - interval in milliseconds between invocations
    /// @returns {number|null} - the event ID for use with clearInterval, or null on error/no script
    v8_function::Register(
        isolate, global, "setInterval", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
            args.GetReturnValue().SetNull();
            auto* isolate = args.GetIsolate();

            if (args.Length() < 2 || !args[0]->IsFunction() || !args[1]->IsNumber()) {
                v8_error::ReportError(args, "invalid params passed to setInterval");
                return;
            }

            auto* script = ScriptEngine::Instance().GetScript(isolate);
            if (!script)
                return;

            auto fn = args[0].As<v8::Function>();
            uint32_t repeatMs = v8_convert::ToUint32(isolate, args[1]);

            auto event = std::make_shared<DelayedEvent>(v8::Global<v8::Function>(isolate, fn), repeatMs);
            script->AddDelayedEvent(event);
            script->PostEvent(event, repeatMs);
            args.GetReturnValue().Set(event->EventId());
        });

    /// @description Cancels a repeating interval created by setInterval, by its event ID.
    /// @signature clearInterval(id: number)
    /// @param id {number} - the event ID returned by setInterval
    /// @returns {null}
    v8_function::Register(
        isolate, global, "clearInterval", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
            args.GetReturnValue().SetNull();
            auto* isolate = args.GetIsolate();

            if (args.Length() < 1 || !args[0]->IsNumber()) {
                v8_error::ReportError(args, "invalid params passed to clearInterval");
                return;
            }

            auto* script = ScriptEngine::Instance().GetScript(isolate);
            if (!script)
                return;

            uint32_t id = v8_convert::ToUint32(isolate, args[0]);
            script->RemoveDelayedEvent(id);
        });

    /// @description Registers an event handler for a named event on the calling script.
    /// @signature addEventListener(eventName: string, callback: function)
    /// @param eventName {string} - the event name to listen for (non-empty)
    /// @param callback {function} - the handler to invoke when the event fires; its arguments depend on the event name
    ///                              (see the events documentation)
    /// @returns {undefined}
    /// @throws {Error} - if the event name is empty
    v8_function::Register(
        isolate, global, "addEventListener", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
            auto* isolate = args.GetIsolate();

            // Reference: silently no-ops if args are missing/wrong type (no JS_ReportError)
            if (args.Length() < 2 || !args[0]->IsString() || !args[1]->IsFunction()) {
                return;
            }

            std::string eventName = v8_convert::ToString(isolate, args[0]);

            if (eventName.empty()) {
                v8_error::ThrowError(isolate, "Event name is invalid!");
                return;
            }

            auto* script = ScriptEngine::Instance().GetScript(isolate);
            if (!script)
                return;

            script->RegisterEvent(eventName, args[1].As<v8::Function>());
        });

    /// @description Unregisters an event handler for a named event on the calling script.
    /// @signature removeEventListener(eventName: string, callback: function)
    /// @param eventName {string} - the event name (non-empty)
    /// @param callback {function} - the same handler reference passed to addEventListener
    /// @returns {undefined}
    /// @throws {Error} - if the event name is empty
    v8_function::Register(
        isolate, global, "removeEventListener", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
            auto* isolate = args.GetIsolate();

            // Reference: silently no-ops if args are missing/wrong type (no JS_ReportError)
            if (args.Length() < 2 || !args[0]->IsString() || !args[1]->IsFunction()) {
                return;
            }

            std::string eventName = v8_convert::ToString(isolate, args[0]);

            if (eventName.empty()) {
                v8_error::ThrowError(isolate, "Event name is invalid!");
                return;
            }

            auto* script = ScriptEngine::Instance().GetScript(isolate);
            if (!script)
                return;

            script->UnregisterEvent(eventName, args[1].As<v8::Function>());
        });

    /// @description Removes all registered handlers for a single named event on the calling script.
    /// @signature clearEvent(eventName: string)
    /// @param eventName {string} - the event whose handlers should all be removed
    /// @returns {undefined}
    v8_function::Register(
        isolate, global, "clearEvent", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
            auto* isolate = args.GetIsolate();

            if (args.Length() < 1 || !args[0]->IsString()) {
                return;
            }

            std::string eventName = v8_convert::ToString(isolate, args[0]);

            auto* script = ScriptEngine::Instance().GetScript(isolate);
            if (!script)
                return;

            script->ClearEvent(eventName);
        });

    /// @description Removes all registered event handlers (for every event name) on the calling script.
    /// @signature clearAllEvents()
    /// @returns {undefined}
    v8_function::Register(
        isolate, global, "clearAllEvents", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
            auto* script = ScriptEngine::Instance().GetScript(args.GetIsolate());
            if (!script)
                return;

            script->ClearAllEvents();
        });
}

}  // namespace d2bs::api::globals
