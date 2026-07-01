#pragma once

#include <spdlog/sinks/base_sink.h>
#include <mutex>

namespace d2bs::js::console {

// spdlog sink that forwards each log entry to game::console::OnMessage with
// source=Log and the raw payload (no pattern formatting, no color parsing).
//
// Registered as one of the default logger's sinks in js::Host::SetupLogging,
// so every named logger obtained via utils::GetLogger inherits it and fans
// framework-internal log entries into the port's OnMessage alongside any
// file / stderr sinks the port chooses to register.
//
// Port-produced entries (print / debugLog / EvaluateEvent::Execute) call
// OnMessage directly; this sink handles only framework-side entries.
class ConsoleSink : public spdlog::sinks::base_sink<std::mutex> {
   protected:
    void sink_it_(const spdlog::details::log_msg& msg) override;
    void flush_() override {}
};

}  // namespace d2bs::js::console
