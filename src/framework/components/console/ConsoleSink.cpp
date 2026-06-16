#include "components/console/ConsoleSink.h"

#include "game/Console.h"

namespace d2bs::framework::console {

namespace {

d2bs::game::console::MessageLevel MapLevel(spdlog::level::level_enum level) {
    using SpdlogLevel = spdlog::level::level_enum;
    using MessageLevel = d2bs::game::console::MessageLevel;
    switch (level) {
        case SpdlogLevel::trace:
            return MessageLevel::Trace;
        case SpdlogLevel::debug:
            return MessageLevel::Debug;
        case SpdlogLevel::info:
            return MessageLevel::Info;
        case SpdlogLevel::warn:
            return MessageLevel::Warn;
        case SpdlogLevel::err:
            return MessageLevel::Error;
        case SpdlogLevel::critical:
            return MessageLevel::Critical;
        case SpdlogLevel::off:
        case SpdlogLevel::n_levels:
        default:
            return MessageLevel::Info;
    }
}

}  // namespace

void ConsoleSink::sink_it_(const spdlog::details::log_msg& msg) {
    // spdlog substitutes fmt placeholders before sinks run, so msg.payload is
    // the already-formatted output (not the format string). We hand that over
    // unchanged - the port decides whether to parse color escapes, add its
    // own timestamp / level prefix, or strip / colorize for rendering.
    d2bs::game::console::OnMessage({
        .source = d2bs::game::console::MessageSource::Log,
        .name = std::string{msg.logger_name.data(), msg.logger_name.size()},
        .level = MapLevel(msg.level),
        .text = std::string{msg.payload.data(), msg.payload.size()},
    });
}

}  // namespace d2bs::framework::console
