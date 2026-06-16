#pragma once

#include <memory>

namespace spdlog {
class logger;
}  // namespace spdlog
namespace v8 {
class Isolate;
}  // namespace v8

namespace d2bs {

/// Returns the per-script logger for isolate (or the current isolate if null). Falls back to a shared "js" logger
/// outside a script context.
std::shared_ptr<spdlog::logger> GetLogger(v8::Isolate* isolate = nullptr);

}  // namespace d2bs
