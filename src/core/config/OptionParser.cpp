#include "config/OptionParser.h"

#include <Windows.h>
#include <shellapi.h>
#include <winternl.h>

#include <algorithm>
#include <cstdint>
#include <cwchar>
#include <span>
#include <string_view>
#include <vector>

namespace d2bs::config {

namespace {

// Command-line switches are pure ASCII, so compare the wide argv token to the
// narrow option name without a codepage conversion.
bool EqualsAscii(std::wstring_view token, std::string_view name) {
    if (token.size() != name.size()) {
        return false;
    }
    for (size_t i = 0; i < token.size(); ++i) {
        if (token[i] != static_cast<wchar_t>(name[i])) {
            return false;
        }
    }
    return true;
}

bool IsSeparator(wchar_t c) {
    return c == L' ' || c == L'\t';
}

// Where one argv token sits in the raw command line, as [begin, end).
struct TokenSpan {
    size_t begin = 0;
    size_t end = 0;
};

// Split the command line the way CommandLineToArgvW does, but yield each
// token's span in the raw buffer instead of its unescaped text - the span is
// what has to be overwritten, and the unescaped text cannot be mapped back to
// it. argv[0] follows its own rule: quotes group it, backslashes do not escape.
std::vector<TokenSpan> TokenSpans(std::wstring_view cmd) {
    std::vector<TokenSpan> spans;
    size_t i = 0;
    while (i < cmd.size() && IsSeparator(cmd[i])) {
        ++i;
    }

    if (i < cmd.size()) {
        const size_t begin = i;
        if (cmd[i] == L'"') {
            ++i;
            while (i < cmd.size() && cmd[i] != L'"') {
                ++i;
            }
            if (i < cmd.size()) {
                ++i;
            }
        } else {
            while (i < cmd.size() && !IsSeparator(cmd[i])) {
                ++i;
            }
        }
        spans.push_back({.begin = begin, .end = i});
    }

    while (i < cmd.size()) {
        while (i < cmd.size() && IsSeparator(cmd[i])) {
            ++i;
        }
        if (i >= cmd.size()) {
            break;
        }
        const size_t begin = i;
        bool isQuoted = false;
        while (i < cmd.size()) {
            const wchar_t c = cmd[i];
            if (c == L'\\') {
                size_t slashes = 0;
                while (i < cmd.size() && cmd[i] == L'\\') {
                    ++slashes;
                    ++i;
                }
                // An even run of backslashes leaves the quote to do its job; an
                // odd one escapes it into a literal character.
                if (i < cmd.size() && cmd[i] == L'"') {
                    if (slashes % 2 == 0) {
                        isQuoted = !isQuoted;
                    }
                    ++i;
                }
                continue;
            }
            if (c == L'"') {
                isQuoted = !isQuoted;
                ++i;
                continue;
            }
            if (!isQuoted && IsSeparator(c)) {
                break;
            }
            ++i;
        }
        spans.push_back({.begin = begin, .end = i});
    }
    return spans;
}

// The command line as the PEB holds it, which is the buffer every external
// reader sees. Usually the same storage GetCommandLineW() returns.
UNICODE_STRING* PebCommandLine() {
    PEB* peb = NtCurrentTeb()->ProcessEnvironmentBlock;
    if (peb == nullptr || peb->ProcessParameters == nullptr) {
        return nullptr;
    }
    UNICODE_STRING& line = peb->ProcessParameters->CommandLine;
    return line.Buffer != nullptr ? &line : nullptr;
}

// What one matched switch costs the line: itself, the value token when it takes
// one, and the whitespace that separated the pair from its neighbour, so the
// cut leaves no double space behind.
TokenSpan CutFor(std::wstring_view cmd, TokenSpan first, TokenSpan last) {
    TokenSpan cut{.begin = first.begin, .end = last.end};
    while (cut.end < cmd.size() && IsSeparator(cmd[cut.end])) {
        ++cut.end;
    }
    if (cut.end == cmd.size()) {
        while (cut.begin > 0 && IsSeparator(cmd[cut.begin - 1])) {
            --cut.begin;
        }
    }
    return cut;
}

// RemoveOptions over the process's own command line, whose page protection
// belongs to whoever allocated it, so ask for write access and hand it back.
size_t ScrubBuffer(wchar_t* text, size_t length, std::span<const OptionName> options) {
    if (length == 0) {
        return 0;
    }
    const size_t bytes = length * sizeof(wchar_t);
    DWORD previous = 0;
    if (VirtualProtect(text, bytes, PAGE_READWRITE, &previous) == 0) {
        return length;
    }
    const size_t remaining = RemoveOptions(std::span<wchar_t>{text, length}, options);
    VirtualProtect(text, bytes, previous, &previous);
    return remaining;
}

}  // namespace

void ParseCommandLine(std::span<const BoundOption> options) {
    const auto* cmdLine = GetCommandLineW();
    if (cmdLine == nullptr) {
        return;
    }

    int32_t argc = 0;
    LPWSTR* argv = CommandLineToArgvW(cmdLine, &argc);
    if (argv == nullptr) {
        return;
    }

    // argv[0] is the program name; skip it.
    for (int32_t i = 1; i < argc; ++i) {
        const std::wstring_view token{argv[i]};
        for (const auto& option : options) {
            if (!EqualsAscii(token, option.name)) {
                continue;
            }
            if (option.takesValue) {
                if (i + 1 < argc) {
                    const std::wstring_view value{argv[++i]};
                    if (!value.empty()) {
                        option.apply(value);
                    }
                }
            } else {
                option.apply({});
            }
            break;
        }
    }

    LocalFree(static_cast<void*>(argv));
}

size_t RemoveOptions(std::span<wchar_t> text, std::span<const OptionName> options) {
    if (text.empty() || options.empty()) {
        return text.size();
    }
    const std::wstring_view cmd{text.data(), text.size()};
    const std::vector<TokenSpan> spans = TokenSpans(cmd);

    std::vector<TokenSpan> cuts;
    // argv[0] is the program name, never an option, even when it spells one.
    for (size_t i = 1; i < spans.size(); ++i) {
        const std::wstring_view token = cmd.substr(spans[i].begin, spans[i].end - spans[i].begin);
        const auto match = std::ranges::find_if(
            options, [token](const OptionName& option) { return EqualsAscii(token, option.name); });
        if (match == options.end()) {
            continue;
        }
        // A value option with nothing after it parsed as nothing, so there is
        // no value token to take with the switch.
        const bool hasValue = match->takesValue && i + 1 < spans.size();
        cuts.push_back(CutFor(cmd, spans[i], hasValue ? spans[i + 1] : spans[i]));
        if (hasValue) {
            ++i;
        }
    }
    if (cuts.empty()) {
        return text.size();
    }

    size_t out = cuts.front().begin;
    size_t in = cuts.front().begin;
    for (const auto& cut : cuts) {
        while (in < cut.begin) {
            text[out++] = text[in++];
        }
        in = cut.end;
    }
    while (in < text.size()) {
        text[out++] = text[in++];
    }
    std::fill(text.begin() + static_cast<std::ptrdiff_t>(out), text.end(), L'\0');
    return out;
}

void RemoveCommandLineOptions(std::span<const OptionName> options) {
    if (options.empty()) {
        return;
    }
    auto* cmdLine = GetCommandLineW();
    if (cmdLine == nullptr) {
        return;
    }
    const size_t length = wcslen(cmdLine);

    // Our tokenizer has to agree with the one that produced the parsed values,
    // or a span could name the wrong token and cut something the game reads.
    // Disagreement means an input we do not model, so leave it alone.
    int32_t argc = 0;
    LPWSTR* argv = CommandLineToArgvW(cmdLine, &argc);
    if (argv == nullptr) {
        return;
    }
    const size_t tokenCount = TokenSpans(std::wstring_view{cmdLine, length}).size();
    LocalFree(static_cast<void*>(argv));
    if (tokenCount != static_cast<size_t>(argc)) {
        return;
    }

    const size_t remaining = ScrubBuffer(cmdLine, length, options);

    // Normally the same storage, in which case the cut already landed and only
    // the declared length is left to correct. The PEB is the copy external
    // readers see, so cut it separately when the two have been split apart.
    UNICODE_STRING* peb = PebCommandLine();
    if (peb == nullptr) {
        return;
    }
    if (peb->Buffer == cmdLine) {
        peb->Length = static_cast<USHORT>(remaining * sizeof(wchar_t));
        return;
    }
    const size_t pebLength = ScrubBuffer(peb->Buffer, peb->Length / sizeof(wchar_t), options);
    peb->Length = static_cast<USHORT>(pebLength * sizeof(wchar_t));
}

}  // namespace d2bs::config
