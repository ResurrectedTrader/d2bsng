#pragma once

#include <string>
#include <string_view>

namespace d2bs::runtime::script {

// V8 origin name for console/chat commands - affects stack trace reporting. Matches reference.
inline constexpr std::string_view COMMAND_LINE_NAME = "Command Line";

// Dispatch a submitted console line. Called by:
//   - GameCallbacks::onConsoleInput (overlay / ImGui / terminal Enter)
//   - DDE Execute / IPC Evaluate handlers (if they opt in)
//
// Behavior:
//   1. Left-trim and split off the first whitespace-delimited token.
//      Empty -> return.
//   2. Match the token (case-insensitive) against the fixed built-in set
//      (start/stop/load/reload/flush/exec). On hit, run the handler with
//      the remainder.
//   3. Else: ScriptEngine::Evaluate(line) - compiles + runs as JS in the
//      console script's isolate. Compile / runtime errors are logged by
//      the EvaluateEvent path; this function does not propagate status.
//
// Fire-and-forget, thread-safe (the underlying ScriptEngine calls are).
void RunCommand(const std::string& line);

// Dispatch a '.'-prefixed chat line (without the dot). Runs only the built-ins above and returns
// true; any other line runs nothing and returns false, so the caller lets it through to the
// chatinput events and the game (reference/d2bs/Helpers.cpp ProcessCommand with
// unprocessedIsCommand = false).
bool RunChatCommand(const std::string& line);

// /reload handler - stops all scripts, briefly settles, then relaunches the
// configured starter (or no-op when waitForProfile is set). Exposed for tests.
void ReloadAll();

}  // namespace d2bs::runtime::script
