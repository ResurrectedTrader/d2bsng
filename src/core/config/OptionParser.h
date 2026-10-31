#pragma once

#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

// Command-line option parsing shared by the backends and by hosts that embed
// one. A backend declares its own options struct and builds a table of
// OptionSpec<Options> through a SpecBuilder; ParseCommandLine walks
// GetCommandLineW() against that table (ParseArguments walks a given argument
// list), and RemoveCommandLineOptions then takes the table's switches back out
// of the process command line.

namespace d2bs::core::config {

// What a switch is for, as its help text shows it: the prose, the heading it is
// listed under, and the value it has when it is not given (empty: nothing to
// say). scripts/extract_api.py reads these string literals out of a backend's
// table into the docs, so they are written for script authors.
struct OptionText {
    std::string_view description;
    std::string_view category;
    std::string_view defaultValue;
};

// One command-line switch: its name, the argument syntax shown in the docs
// (empty for a bare flag), its help text, and the handler that folds the parsed
// value into the options struct. A flag's handler is called with an empty value;
// a value option consumes the following argv token and receives it. The handler
// returns false to reject a value.
template <typename Options>
struct OptionSpec {
    std::string_view name;
    std::string_view syntax;
    std::string_view description;
    std::string_view category;
    std::string_view defaultValue;
    std::function<bool(Options&, std::wstring_view)> apply;

    [[nodiscard]] bool TakesValue() const { return !syntax.empty(); }
};

// Collects a backend's option table. scripts/extract_api.py reads the lod114d
// backend's builder - the Add("-name", "<syntax>", {...}) string literals - into
// the docs, so that table is the single source of truth for parsing, for a
// host's help text and for the documentation. A handler may return void (it
// accepts every value) or bool.
template <typename Options>
struct SpecBuilder {
    std::vector<OptionSpec<Options>> specs;

    template <typename Apply>
    void Add(std::string_view name, std::string_view syntax, const OptionText& text, Apply apply) {
        specs.push_back({.name = name,
                         .syntax = syntax,
                         .description = text.description,
                         .category = text.category,
                         .defaultValue = text.defaultValue,
                         .apply = Wrap(std::move(apply))});
    }

    template <typename Apply>
    void Add(std::string_view name, std::string_view syntax, Apply apply) {
        Add(name, syntax, OptionText{}, std::move(apply));
    }

   private:
    template <typename Apply>
    static std::function<bool(Options&, std::wstring_view)> Wrap(Apply apply) {
        if constexpr (std::is_void_v<std::invoke_result_t<Apply&, Options&, std::wstring_view>>) {
            return [apply = std::move(apply)](Options& options, std::wstring_view value) mutable {
                apply(options, value);
                return true;
            };
        } else {
            return std::move(apply);
        }
    }
};

// One spec with its options struct bound, as the argv walker sees it.
struct BoundOption {
    std::string_view name;
    bool takesValue = false;
    std::function<bool(std::wstring_view)> apply;
};

// How ParseArguments treats an argument that no option claims.
struct ParseSettings {
    // False (the default, and what the DLL does): skip it - it belongs to the
    // game, or to another layer that reads the same command line. True: report
    // it as an error.
    bool isUnknownAnError = false;
};

// One argument ParseArguments could not apply. `option` and `value` are UTF-8;
// `message` is a one-line, ASCII-safe explanation ("unknown switch -x",
// "-title needs a value", "bad -proxy 'x'").
struct ParseError {
    std::string option;
    std::string value;
    std::string message;
};

// Walk `args` - the arguments alone, without a program name - and apply the
// first option whose name equals each one. A value option consumes the next
// argument; when there is none it is reported, and an empty value is skipped
// without calling its handler. A handler that returns false is reported with the
// value it rejected. Parsing carries on past an error, so every one is reported.
std::vector<ParseError> ParseArguments(std::span<const std::wstring_view> args, std::span<const BoundOption> options,
                                       ParseSettings settings = {});

template <typename Options>
std::vector<BoundOption> BindOptions(const std::vector<OptionSpec<Options>>& specs, Options& out) {
    std::vector<BoundOption> bound;
    bound.reserve(specs.size());
    for (const auto& spec : specs) {
        auto apply = [&spec, &out](std::wstring_view value) {
            return spec.apply(out, value);
        };
        bound.push_back({.name = spec.name, .takesValue = spec.TakesValue(), .apply = std::move(apply)});
    }
    return bound;
}

template <typename Options>
std::vector<ParseError> ParseArguments(std::span<const std::wstring_view> args,
                                       const std::vector<OptionSpec<Options>>& specs, Options& out,
                                       ParseSettings settings = {}) {
    const std::vector<BoundOption> bound = BindOptions(specs, out);
    return ParseArguments(args, std::span<const BoundOption>{bound}, settings);
}

// ParseArguments over GetCommandLineW() (argv[0], the program name, is
// skipped), with unknown tokens ignored and every error dropped: the process
// command line also carries the game's own switches, and a bad value leaves the
// option at its default.
void ParseCommandLine(std::span<const BoundOption> options);

template <typename Options>
void ParseCommandLine(const std::vector<OptionSpec<Options>>& specs, Options& out) {
    const std::vector<BoundOption> bound = BindOptions(specs, out);
    ParseCommandLine(std::span<const BoundOption>{bound});
}

// An option as the remover needs it: what to match, and whether a value token
// follows it and goes with it.
struct OptionName {
    std::string_view name;
    bool takesValue = false;
};

// Take every one of `options` back out of the process command line once it has
// been parsed, leaving only the arguments the game itself was given. Call it
// after ParseCommandLine; the parsed values are already cached, so nothing
// reads the command line for them again.
//
// This removes the whole trace, not just the credentials in it: a value that
// was a credential is gone, and so is the evidence of which switches were
// passed at all.
//
// Three buffers are covered. The one GetCommandLineW() returns is cut in
// place. RTL_USER_PROCESS_PARAMETERS.CommandLine - the copy a remote read of
// the PEB, Task Manager, or WMI's Win32_Process.CommandLine sees - is usually
// that same storage, and is cut separately on the systems where it is not.
// The ANSI line kernelbase built during process init is re-encoded from the
// cut wide line, not cut itself, because the two do not always split into the
// same tokens; without it the whole line would stay readable through one
// GetCommandLineA() call.
//
// The cut happens once the options are parsed, so it cannot run before the
// process was created. Anything that records a command line AT creation -
// Sysmon, ETW's Kernel-Process provider, an EDR's process-creation callback,
// the parent itself - has already recorded the original. This defeats reading
// the line afterwards, not capturing it as the process starts.
//
// What it does NOT reach, so do not read it as erasure:
//   - any copy another module's CRT took at its own startup, including the
//     game's argv array.
//   - the parsed value itself, which a backend may hold for the life of the
//     process because the game needs it.
// It removes the switches from what reads a process's command line - Task
// Manager, WMI's Win32_Process.CommandLine, a remote read of the PEB, and
// GetCommandLineA() / GetCommandLineW() in this process - and nothing beyond
// that.
void RemoveCommandLineOptions(std::span<const OptionName> options);

// The cut itself, over a caller-owned buffer: remove each of `options` and, for
// the ones that take a value, the token that follows it; shift what is left
// down over the gap; zero the tail that frees; return the length that remains.
// Tokenisation matches CommandLineToArgvW, so a quoted value is cut whole.
// Separate from RemoveCommandLineOptions, which adds the process command line
// and its page protection, so that this half can be tested.
size_t RemoveOptions(std::span<wchar_t> text, std::span<const OptionName> options);

template <typename Options>
void RemoveCommandLineOptions(const std::vector<OptionSpec<Options>>& specs) {
    std::vector<OptionName> names;
    names.reserve(specs.size());
    for (const auto& spec : specs) {
        names.push_back({.name = spec.name, .takesValue = spec.TakesValue()});
    }
    RemoveCommandLineOptions(std::span<const OptionName>{names});
}

}  // namespace d2bs::core::config
