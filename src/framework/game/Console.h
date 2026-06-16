#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// Port-visible interface for console output, visibility, and color-code helpers. Inline helpers are defined here
// because game/ has no .cpp files.

namespace d2bs::game::console {

// ---------------------------------------------------------------------------
// Types
// ---------------------------------------------------------------------------

// D2 palette color codes. The underlying char values are the bytes that
// appear after `\xFF c` in D2 color escapes - e.g. `\xFFc2` yields
// ColorCode::NeonGreen.
enum class ColorCode : char {
    White = '0',
    Red = '1',
    NeonGreen = '2',
    Blue = '3',
    DarkGold = '4',
    Grey = '5',
    Black = '6',
    LightGold = '7',
    Orange = '8',
    Yellow = '9',
    DarkGreen = ':',
    Purple = ';',
    Green = '<',
};

// A run of text tagged with a D2 palette color code. Returned by SplitByColor;
// also a standalone utility type ports may use in their own rendering.
struct Segment {
    ColorCode colorCode;  // '0' = default/white
    std::string text;     // plain text, codes stripped
};

// Where the message came from. Set explicitly at the call site:
//   - Print:         user-facing output from JS print() / debugLog() running
//                    in any non-console-mode script. Routed to the log.
//                    Distinguish the two at the port by `level`:
//                       level == Info  -> came from print()
//                       level == Debug -> came from debugLog()
//   - ConsolePrint:  same as Print, but emitted from a script running in
//                    ScriptMode::Console (the REPL isolate). Routed to the
//                    console window. The call site picks this based on the
//                    calling script's mode so downstream routing doesn't
//                    have to match on script names.
//   - EvaluateResult: EvaluateEvent::Execute (eval result or thrown error)
//   - Log:           ConsoleSink (framework-internal spdlog entries)
enum class MessageSource : uint8_t {
    Print,
    ConsolePrint,
    EvaluateResult,
    Log,
};

// Severity. Matches spdlog's level semantics so the sink can forward losslessly.
// For direct callers (Print / EvaluateResult), pick the level that fits:
// print -> Info, debugLog -> Debug, eval result -> Info, eval error -> Error.
enum class MessageLevel : uint8_t {
    Trace,
    Debug,
    Info,
    Warn,
    Error,
    Critical,
};

// A log entry delivered to the port. Owned fields - safe to retain past the
// call if the port needs to queue for render-thread drain.
//
// `text` holds the raw string as produced by the caller, with color escapes
// embedded if any. 1.14d ports pass it straight to D2WIN_DrawText (which
// parses the escapes natively). Other ports call SplitByColor(text) or
// StripColor(text) as needed (declarations below).
struct Message {
    MessageSource source;
    std::string name;
    MessageLevel level;
    std::string text;
};

// ---------------------------------------------------------------------------
// Implemented by the port, called by the framework
// ---------------------------------------------------------------------------

// Called directly by print() / debugLog() / EvaluateEvent::Execute, and
// indirectly by the framework's ConsoleSink (for framework-internal log
// entries). Must be thread-safe - can fire from any script thread, the
// game thread, or spdlog's logging thread.
void OnMessage(const Message& msg);

// Console visibility (for showConsole() / hideConsole() JS globals and
// the framework's VK_HOME hotkey). A port with no distinct overlay concept
// may no-op these.
void Show();
void Hide();
void Toggle();

// ---------------------------------------------------------------------------
// Implemented by the framework (inline), callable by the port
// ---------------------------------------------------------------------------
//
// These are opt-in utilities - a port that passes msg.text straight to a
// color-aware renderer (e.g. 1.14d D2WIN_DrawText) never needs them. Ports
// that want segment-level rendering (ImGui) or plain text (terminal / file)
// use them on demand.
//
// Defined inline because `src/framework/game/` is interface-only by
// convention - no .cpp files live there. The bodies are small.

// Split `raw` into (code, text) runs. Recognizes two encodings:
//
//   \xFF c X      Latin-1 "\xFFcX" - the form d2bsng emits and the form
//                 reference/d2bs passes through to D2WIN_DrawText.
//
//   \xFF c X      6-char kolbot double-escape: the literal backslash-x-f-f-c
//                 followed by the color char. Some kolbot scripts write the
//                 escape as text instead of a real 0xFF byte; case-insensitive
//                 on the x/X and f/F.
//
// Runs without an explicit code default to `startingColor`. Sequences that
// are incomplete at EOF (e.g. trailing `\xFF c` with no X) pass through as
// literal bytes. Unknown codes (outside the ColorCode enumeration) are
// cast into Segment::colorCode anyway - ColorCode's fixed `char` underlying
// type means any byte value is representable without UB. Port may choose
// to treat unknown codes as White.
// Also recognises a 4-byte UTF-8 form (0xC3 0xBF 'c' X): V8's Utf8Value
// re-encodes U+00FF as 0xC3 0xBF, so JS-produced strings with the native
// colour byte arrive here in UTF-8, not raw Latin-1.
inline bool DetectColorSequence(std::string_view raw, size_t i, size_t& seqLen, size_t& codeOffset) {
    const auto remaining = raw.size() - i;

    if (remaining >= 3 && static_cast<uint8_t>(raw[i]) == 0xFF && raw[i + 1] == 'c') {
        seqLen = 3;
        codeOffset = 2;
        return true;
    }
    if (remaining >= 4 && static_cast<uint8_t>(raw[i]) == 0xC3 && static_cast<uint8_t>(raw[i + 1]) == 0xBF &&
        raw[i + 2] == 'c') {
        seqLen = 4;
        codeOffset = 3;
        return true;
    }
    if (remaining >= 6 && raw[i] == '\\' &&          //
        (raw[i + 1] == 'x' || raw[i + 1] == 'X') &&  //
        (raw[i + 2] == 'f' || raw[i + 2] == 'F') &&  //
        (raw[i + 3] == 'f' || raw[i + 3] == 'F') &&  //
        raw[i + 4] == 'c') {
        seqLen = 6;
        codeOffset = 5;
        return true;
    }
    return false;
}

inline std::vector<Segment> SplitByColor(std::string_view raw, ColorCode startingColor = ColorCode::White) {
    std::vector<Segment> out;
    std::string buffer;
    auto currentCode = startingColor;

    auto flush = [&]() {
        if (!buffer.empty()) {
            out.push_back({.colorCode = currentCode, .text = std::move(buffer)});
            buffer.clear();
        }
    };

    for (size_t i = 0; i < raw.size();) {
        size_t seqLen = 0;
        size_t codeOffset = 0;
        if (DetectColorSequence(raw, i, seqLen, codeOffset)) {
            flush();
            currentCode = static_cast<ColorCode>(raw[i + codeOffset]);
            i += seqLen;
        } else {
            buffer.push_back(raw[i]);
            ++i;
        }
    }
    flush();
    return out;
}

// Strip all color codes, return plain text. Recognizes the same three
// encodings as SplitByColor. Cheaper - no per-segment allocations.
inline std::string StripColor(std::string_view raw) {
    std::string out;
    out.reserve(raw.size());
    for (size_t i = 0; i < raw.size();) {
        size_t seqLen = 0;
        size_t codeOffset = 0;
        if (DetectColorSequence(raw, i, seqLen, codeOffset)) {
            i += seqLen;
        } else {
            out.push_back(raw[i]);
            ++i;
        }
    }
    return out;
}

}  // namespace d2bs::game::console
