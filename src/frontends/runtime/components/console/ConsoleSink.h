#pragma once

#include <spdlog/sinks/base_sink.h>
#include <mutex>

namespace d2bs::runtime::console {

// spdlog sink that forwards each log entry to game::console::OnMessage with
// source=Log and the raw payload (no pattern formatting, no color parsing).
//
// Added to the fan-out sink every named logger shares (runtime::Host::SetupLogging
// -> utils::AddLogSink), so framework-internal entries from any logger - even
// one created before SetupLogging ran - reach the port's OnMessage alongside
// the file sink.
//
// Port-produced entries (print / debugLog / EvaluateEvent::Execute) call
// OnMessage directly; this sink handles only framework-side entries.
class ConsoleSink : public spdlog::sinks::base_sink<std::mutex> {
   protected:
    void sink_it_(const spdlog::details::log_msg& msg) override;
    void flush_() override {}
};

}  // namespace d2bs::runtime::console
