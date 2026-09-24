#pragma once

#include <memory>

namespace spdlog {
class logger;
}  // namespace spdlog
namespace ub {
class Isolate;
}  // namespace ub

namespace d2bs {

/// Returns the per-script logger for isolate, or for the script whose thread this is if null. Falls back to a
/// shared "js" logger outside a script context.
std::shared_ptr<spdlog::logger> GetLogger(ub::Isolate* isolate = nullptr);

}  // namespace d2bs
