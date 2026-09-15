#include "config/OptionParser.h"

#include <Windows.h>
#include <shellapi.h>
#include <winternl.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <span>
#include <string>
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

template <typename CharT>
bool IsSeparator(CharT c) {
    return c == static_cast<CharT>(' ') || c == static_cast<CharT>('\t');
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
template <typename CharT>
std::vector<TokenSpan> TokenSpans(std::basic_string_view<CharT> cmd) {
    constexpr CharT QUOTE = static_cast<CharT>('"');
    constexpr CharT BACKSLASH = static_cast<CharT>('\\');
    std::vector<TokenSpan> spans;
    size_t i = 0;
    while (i < cmd.size() && IsSeparator(cmd[i])) {
        ++i;
    }

    if (i < cmd.size()) {
        const size_t begin = i;
        if (cmd[i] == QUOTE) {
            ++i;
            while (i < cmd.size() && cmd[i] != QUOTE) {
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
            const CharT c = cmd[i];
            if (c == BACKSLASH) {
                size_t slashes = 0;
                while (i < cmd.size() && cmd[i] == BACKSLASH) {
                    ++slashes;
                    ++i;
                }
                // An even run of backslashes leaves the quote to do its job; an
                // odd one escapes it into a literal character.
                if (i < cmd.size() && cmd[i] == QUOTE) {
                    if (slashes % 2 == 0) {
                        isQuoted = !isQuoted;
                    }
                    ++i;
                }
                continue;
            }
            if (c == QUOTE) {
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
template <typename CharT>
TokenSpan CutFor(std::basic_string_view<CharT> cmd, TokenSpan first, TokenSpan last) {
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

// The tokens one registered option occupies: the switch, and the value token
// when it takes one.
struct TokenRange {
    size_t first = 0;
    size_t last = 0;
};

// Which tokens the registered options occupy, matched against the UNESCAPED
// argument because that is what the parse matched. Matching raw buffer text
// would disagree wherever quoting differs: a launcher that quotes everything
// writes "-profile", which the parser recognises and a raw match would not, so
// nothing would be removed - and worse, a switch the parser had already eaten
// as another option's value would match here and be cut along with the argument
// after it, taking one of the game's own arguments with it.
std::vector<TokenRange> OptionTokens(const std::wstring& cmd, size_t tokenCount, std::span<const OptionName> options) {
    std::vector<TokenRange> ranges;
    int32_t argc = 0;
    LPWSTR* argv = CommandLineToArgvW(cmd.c_str(), &argc);
    if (argv == nullptr) {
        return ranges;
    }
    // Our split has to agree with the one that produced the parsed values, or a
    // span names a different token than the name we matched. Disagreement means
    // an input we do not model, so touch nothing.
    if (static_cast<size_t>(argc) == tokenCount) {
        // argv[0] is the program name, never an option, even when it spells one.
        for (size_t i = 1; i < tokenCount; ++i) {
            const std::wstring_view token{argv[i]};
            const auto match = std::ranges::find_if(
                options, [token](const OptionName& option) { return EqualsAscii(token, option.name); });
            if (match == options.end()) {
                continue;
            }
            // A value option with nothing after it parsed as nothing, so there
            // is no value token to take with the switch.
            const bool hasValue = match->takesValue && i + 1 < tokenCount;
            ranges.push_back({.first = i, .last = hasValue ? i + 1 : i});
            if (hasValue) {
                ++i;
            }
        }
    }
    LocalFree(static_cast<void*>(argv));
    return ranges;
}

// Cut `ranges` out of `text`, whatever its character width: the ANSI and wide
// command lines hold the same text, so the same token indices apply to both.
// Only if this buffer splits into the same number of tokens, which is checked -
// otherwise it is not the line those indices were computed from.
template <typename CharT>
size_t CutTokens(std::span<CharT> text, const std::vector<TokenRange>& ranges, size_t tokenCount) {
    if (text.empty() || ranges.empty()) {
        return text.size();
    }
    const std::basic_string_view<CharT> cmd{text.data(), text.size()};
    const std::vector<TokenSpan> spans = TokenSpans<CharT>(cmd);
    if (spans.size() != tokenCount) {
        return text.size();
    }
    std::vector<TokenSpan> cuts;
    cuts.reserve(ranges.size());
    for (const auto& range : ranges) {
        if (range.last >= spans.size()) {
            return text.size();
        }
        cuts.push_back(CutFor<CharT>(cmd, spans[range.first], spans[range.last]));
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
    std::fill(text.begin() + static_cast<std::ptrdiff_t>(out), text.end(), static_cast<CharT>(0));
    return out;
}

// CutTokens over a buffer the process already owns, whose page protection
// belongs to whoever allocated it, so ask for write access and hand it back.
template <typename CharT>
size_t ScrubBuffer(CharT* text, size_t length, const std::vector<TokenRange>& ranges, size_t tokenCount) {
    if (length == 0) {
        return 0;
    }
    const size_t bytes = length * sizeof(CharT);
    DWORD previous = 0;
    if (VirtualProtect(text, bytes, PAGE_READWRITE, &previous) == 0) {
        return length;
    }
    const size_t remaining = CutTokens(std::span<CharT>{text, length}, ranges, tokenCount);
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
    // A copy, because CommandLineToArgvW needs a terminator and the PEB's
    // command line is a counted string that need not have one.
    const std::wstring cmd{text.data(), text.size()};
    const size_t tokenCount = TokenSpans<wchar_t>(cmd).size();
    return CutTokens(text, OptionTokens(cmd, tokenCount, options), tokenCount);
}

void RemoveCommandLineOptions(std::span<const OptionName> options) {
    if (options.empty()) {
        return;
    }
    auto* wide = GetCommandLineW();
    if (wide == nullptr) {
        return;
    }
    const std::wstring cmd{wide};
    const size_t tokenCount = TokenSpans<wchar_t>(cmd).size();
    const std::vector<TokenRange> ranges = OptionTokens(cmd, tokenCount, options);
    if (ranges.empty()) {
        return;
    }

    const size_t remaining = ScrubBuffer(wide, cmd.size(), ranges, tokenCount);

    // The PEB's copy is what a remote read, Task Manager and WMI report.
    // Usually the same storage GetCommandLineW returns, in which case the cut
    // has already landed and only the declared length is left to correct.
    if (UNICODE_STRING* peb = PebCommandLine(); peb != nullptr) {
        if (peb->Buffer == wide) {
            peb->Length = static_cast<USHORT>(remaining * sizeof(wchar_t));
        } else {
            const size_t pebLength = ScrubBuffer(peb->Buffer, peb->Length / sizeof(wchar_t), ranges, tokenCount);
            peb->Length = static_cast<USHORT>(pebLength * sizeof(wchar_t));
        }
    }

    // kernelbase built an ANSI copy during process init and GetCommandLineA
    // still hands out the original, so skipping it would leave the whole line
    // readable in one call. Separate storage, same text, so the same tokens.
    if (auto* narrow = GetCommandLineA(); narrow != nullptr) {
        ScrubBuffer(narrow, std::strlen(narrow), ranges, tokenCount);
    }
}

}  // namespace d2bs::config
