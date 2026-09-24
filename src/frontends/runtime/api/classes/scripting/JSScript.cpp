#include "JSScript.h"

#include <chrono>
#include <memory>

#include "api/core/Convert.h"
#include "api/core/Error.h"
#include "components/events/Events.h"

namespace d2bs::api::classes {

// Resolves the receiver's handle to a live Script in one call.
static std::shared_ptr<Script> GetScript(const ub::CallbackContextBase& info) {
    auto* handle = JSScript::Unwrap(info.This());
    if (!handle) {
        return nullptr;
    }
    return ScriptEngine::Instance().GetScript(handle->threadId);
}

void JSScript::Configure(const ub::Class<ScriptHandle>& cls) {
    /// @description The script's short file name.
    /// @type {string}
    Property(
        cls, "name", +[](const ub::Local<ub::Name>&, const ub::PropertyCallbackInfo& info) {
            auto script = GetScript(info);
            if (!script) {
                return;
            }
            info.GetReturnValue().Set(convert::ToJS(info.GetIsolate(), script->GetName()));
        });

    /// @description true for an out-of-game script (menu/console), false for an in-game script.
    /// @type {boolean}
    Property(
        cls, "type", +[](const ub::Local<ub::Name>&, const ub::PropertyCallbackInfo& info) {
            auto script = GetScript(info);
            if (!script) {
                return;
            }
            // true = out-of-game (OutOfGame or Console), false = in-game (InGame)
            const bool isOutOfGame = script->GetMode() != ScriptMode::InGame;
            info.GetReturnValue().Set(isOutOfGame);
        });

    /// @description true while the script is in the Running state.
    /// @type {boolean}
    Property(
        cls, "running", +[](const ub::Local<ub::Name>&, const ub::PropertyCallbackInfo& info) {
            auto script = GetScript(info);
            if (!script) {
                return;
            }
            info.GetReturnValue().Set(script->GetState() == ScriptState::Running);
        });

    /// @description The script's native Win32 thread ID.
    /// @type {number}
    Property(
        cls, "threadid", +[](const ub::Local<ub::Name>&, const ub::PropertyCallbackInfo& info) {
            auto script = GetScript(info);
            if (!script) {
                return;
            }
            info.GetReturnValue().Set(script->GetNativeThreadId());
        });

    /// @description Used JS heap size in bytes for this script's engine (0 when stats unavailable).
    /// @type {number}
    Property(
        cls, "memory", +[](const ub::Local<ub::Name>&, const ub::PropertyCallbackInfo& info) {
            auto* handle = Unwrap(info.This());
            if (!handle) {
                return;
            }
            auto script = ScriptEngine::Instance().GetScript(handle->threadId);
            if (!script) {
                return;
            }

            // Force fresh stats when queried from the script's own thread.
            if (handle->threadId == std::this_thread::get_id()) {
                script->UpdateHeapStats(std::chrono::steady_clock::now(), true);
            }
            auto stats = script->GetCachedHeapStats();
            if (!stats) {
                info.GetReturnValue().Set(0);
                return;
            }
            info.GetReturnValue().Set(stats->usedBytes);
        });

    /// @description Advances this Script handle in place to the next script in the engine's list.
    /// @signature getNext()
    /// @returns {boolean|undefined} - true if advanced; undefined when no further script exists.
    Method(
        cls, "getNext", +[](const ub::CallbackInfo& args) {
            auto* handle = Unwrap(args.This());
            if (!handle) {
                return;
            }

            auto scripts = ScriptEngine::Instance().GetAllScripts();

            // Find current script in list, then move to next
            bool foundCurrent = false;
            for (auto& script : scripts) {
                if (foundCurrent) {
                    handle->threadId = script->GetThreadId();
                    args.GetReturnValue().Set(true);
                    return;
                }
                if (script->GetThreadId() == handle->threadId) {
                    foundCurrent = true;
                }
            }

            // No more scripts - return undefined (JSVAL_VOID in reference)
        });

    /// @description Pauses the referenced script (no-op unless it is in the Running state).
    /// @signature pause()
    /// @returns {null} - Always null.
    Method(
        cls, "pause", +[](const ub::CallbackInfo& args) {
            args.GetReturnValue().SetNull();
            auto script = GetScript(args);
            if (script && script->GetState() == ScriptState::Running) {
                script->Pause();
            }
        });

    /// @description Resumes the referenced script (no-op unless it is in the Paused state).
    /// @signature resume()
    /// @returns {null} - Always null.
    Method(
        cls, "resume", +[](const ub::CallbackInfo& args) {
            args.GetReturnValue().SetNull();
            auto script = GetScript(args);
            if (script && script->GetState() == ScriptState::Paused) {
                script->Resume();
            }
        });

    /// @description Blocks the calling script until the referenced script finishes.
    /// @signature join()
    /// @returns {null} - Always null.
    /// @throws {Error} - When a script tries to join itself (would deadlock).
    Method(
        cls, "join", +[](const ub::CallbackInfo& args) {
            args.GetReturnValue().SetNull();
            auto script = GetScript(args);
            if (script) {
                if (script->GetThreadId() == std::this_thread::get_id()) {
                    error::ThrowError(args.GetIsolate(), "Cannot join a script from its own thread");
                    return;
                }
                script->Join();
            }
        });

    /// @description Stops the referenced script (no-op unless it is in the Running or Paused state).
    /// @signature stop()
    /// @returns {null} - Always null.
    Method(
        cls, "stop", +[](const ub::CallbackInfo& args) {
            args.GetReturnValue().SetNull();
            auto script = GetScript(args);
            if (!script) {
                return;
            }
            const auto state = script->GetState();
            if (state == ScriptState::Running || state == ScriptState::Paused) {
                script->Stop();
            }
        });

    /// @description Sends arguments to the referenced script's "scriptmsg" event handlers (deep-copied;
    ///   unserializable values become undefined).
    /// @signature send(arg: any, ...rest: any)
    /// @param arg {any} - First value to send.
    /// @param rest {any} - Additional values to send.
    /// @returns {null} - Always null.
    Method(
        cls, "send", +[](const ub::CallbackInfo& args) {
            args.GetReturnValue().SetNull();

            if (args.Length() < 1) {
                return;
            }

            auto script = GetScript(args);
            if (!script || script->GetState() != ScriptState::Running) {
                return;
            }

            auto evt = std::make_shared<BroadcastEvent>(args);
            script->ExecuteEvent(evt);
        });
}

}  // namespace d2bs::api::classes
