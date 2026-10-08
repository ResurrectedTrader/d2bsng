#pragma once

#include <memory>

namespace spdlog {
class logger;
}  // namespace spdlog
namespace v8 {
class Isolate;
class TryCatch;
}  // namespace v8

namespace d2bs::runtime::script {

/// Returns the per-script logger for isolate (or the current isolate if null). Falls back to a shared "js" logger
/// outside a script context.
std::shared_ptr<spdlog::logger> GetLogger(v8::Isolate* isolate = nullptr);

/// Reports an uncaught exception from a callback run for isolate's script (an event handler or a timer) the same way as
/// an uncaught error in the script body: logged with its file and line, and QuitOnError applies
/// (reference/d2bs/ScriptEngine.cpp reportError).
void ReportHandlerException(v8::Isolate* isolate, v8::TryCatch& tryCatch);

}  // namespace d2bs::runtime::script
