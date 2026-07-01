#include "JSScript.h"

#include "components/events/Events.h"

namespace d2bs::api::classes {

// GetScript: unwraps ScriptHandle and resolves to a live Script in one call.
static std::shared_ptr<Script> GetScriptFromHandle(ScriptHandle* handle) {
    if (!handle)
        return nullptr;
    return ScriptEngine::Instance().GetScript(handle->threadId);
}

template <typename InfoT>
static std::shared_ptr<Script> GetScript(const InfoT& info) {
    if constexpr (std::is_same_v<InfoT, v8::FunctionCallbackInfo<v8::Value>>) {
        return GetScriptFromHandle(JSScript::Unwrap(info.This()));
    } else {
        return GetScriptFromHandle(JSScript::Unwrap(info.Holder()));
    }
}

void JSScript::ConfigureTemplate(v8::Isolate* isolate, v8::Local<v8::FunctionTemplate> tpl) {
    auto inst = tpl->InstanceTemplate();
    auto proto = tpl->PrototypeTemplate();

    /// @description The script's short file name.
    /// @type {string}
    Property(
        isolate, inst, "name", +[](v8::Local<v8::Name> property, const v8::PropertyCallbackInfo<v8::Value>& info) {
            auto script = GetScript(info);
            if (!script)
                return;
            info.GetReturnValue().Set(v8_convert::ToV8(info.GetIsolate(), script->GetName()));
        });

    /// @description true for an out-of-game script (menu/console), false for an in-game script.
    /// @type {boolean}
    Property(
        isolate, inst, "type", +[](v8::Local<v8::Name> property, const v8::PropertyCallbackInfo<v8::Value>& info) {
            auto script = GetScript(info);
            if (!script)
                return;
            // true = out-of-game (OutOfGame or Console), false = in-game (InGame)
            bool isOutOfGame = script->GetMode() != ScriptMode::InGame;
            info.GetReturnValue().Set(isOutOfGame);
        });

    /// @description true while the script is in the Running state.
    /// @type {boolean}
    Property(
        isolate, inst, "running", +[](v8::Local<v8::Name> property, const v8::PropertyCallbackInfo<v8::Value>& info) {
            auto script = GetScript(info);
            if (!script)
                return;
            info.GetReturnValue().Set(script->GetState() == ScriptState::Running);
        });

    /// @description The script's native Win32 thread ID.
    /// @type {number}
    Property(
        isolate, inst, "threadid", +[](v8::Local<v8::Name> property, const v8::PropertyCallbackInfo<v8::Value>& info) {
            auto script = GetScript(info);
            if (!script)
                return;
            info.GetReturnValue().Set(script->GetNativeThreadId());
        });

    /// @description Used JS heap size in bytes for this script's V8 runtime (0 when stats unavailable).
    /// @type {number}
    Property(
        isolate, inst, "memory", +[](v8::Local<v8::Name> property, const v8::PropertyCallbackInfo<v8::Value>& info) {
            auto* handle = Unwrap(info.Holder());
            auto script = GetScriptFromHandle(handle);
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
            info.GetReturnValue().Set(stats->used_heap_size());
        });

    /// @description Advances this Script handle in place to the next script in the engine's list.
    /// @signature getNext()
    /// @returns {boolean|undefined} - true if advanced; undefined when no further script exists.
    Method(
        isolate, proto, "getNext", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
            auto* handle = Unwrap(args.This());
            if (!handle) {
                return;
            }

            auto& engine = ScriptEngine::Instance();
            auto scripts = engine.GetAllScripts();

            // Find current script in list, then move to next
            bool foundCurrent = false;
            for (auto& script : scripts) {
                if (foundCurrent) {
                    // Found next script - update handle to point to it
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
        isolate, proto, "pause", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
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
        isolate, proto, "resume", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
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
        isolate, proto, "join", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
            args.GetReturnValue().SetNull();
            auto script = GetScript(args);
            if (script) {
                // Prevent self-join deadlock
                if (script->GetThreadId() == std::this_thread::get_id()) {
                    v8_error::ThrowError(args.GetIsolate(), "Cannot join a script from its own thread");
                    return;
                }
                script->Join();
            }
        });

    /// @description Stops the referenced script (no-op unless it is in the Running or Paused state).
    /// @signature stop()
    /// @returns {null} - Always null.
    Method(
        isolate, proto, "stop", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
            args.GetReturnValue().SetNull();
            auto script = GetScript(args);
            if (!script)
                return;
            auto state = script->GetState();
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
        isolate, proto, "send", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
            args.GetReturnValue().SetNull();

            if (args.Length() < 1)
                return;

            auto script = GetScript(args);
            if (!script || script->GetState() != ScriptState::Running) {
                return;
            }

            // Serialize arguments using V8 ValueSerializer and dispatch as BroadcastEvent
            auto evt = std::make_shared<BroadcastEvent>(args);
            script->ExecuteEvent(evt);
        });
}

v8::Local<v8::Object> JSScript::Create(v8::Isolate* isolate, Script* script) {
    if (!script) {
        return {};
    }

    auto context = isolate->GetCurrentContext();

    // Create handle with thread ID for safe lookup
    auto handle = std::make_unique<ScriptHandle>(script->GetThreadId());
    return CreateInstance(isolate, context, std::move(handle));
}

}  // namespace d2bs::api::classes
