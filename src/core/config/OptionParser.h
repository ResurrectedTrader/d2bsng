#pragma once

#include <functional>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

// Command-line option parsing shared by the backends. A backend declares its
// own options struct and builds a table of OptionSpec<Options> through a
// SpecBuilder; ParseCommandLine walks GetCommandLineW() against that table, and
// RemoveCommandLineOptions then takes the table's switches back out of it.

namespace d2bs::config {

// One command-line switch: its name, the argument syntax shown in the docs
// (empty for a bare flag), and the handler that folds the parsed value into
// the options struct. A flag's handler is called with an empty value; a value
// option consumes the following argv token and receives it.
template <typename Options>
struct OptionSpec {
    std::string_view name;
    std::string_view syntax;
    std::function<void(Options&, std::wstring_view)> apply;

    [[nodiscard]] bool TakesValue() const { return !syntax.empty(); }
};

// Collects a backend's option table. scripts/extract_api.py reads the lod114d
// backend's builder - the Add("-name", "<syntax>") string literals plus the
// preceding /// doc block - into the docs, the same way it reads the
// CompatibilityFlags catalog, so that table is the single source of truth for
// both parsing and documentation.
template <typename Options>
struct SpecBuilder {
    std::vector<OptionSpec<Options>> specs;

    void Add(std::string_view name, std::string_view syntax, std::function<void(Options&, std::wstring_view)> apply) {
        specs.push_back({.name = name, .syntax = syntax, .apply = std::move(apply)});
    }
};

// One spec with its options struct bound, as the argv walker sees it.
struct BoundOption {
    std::string_view name;
    bool takesValue = false;
    std::function<void(std::wstring_view)> apply;
};

// Walk GetCommandLineW() (argv[0], the program name, is skipped) and apply the
// first option whose name equals each token. A value option consumes the next
// token and is skipped when that token is missing or empty; unknown tokens are
// ignored.
void ParseCommandLine(std::span<const BoundOption> options);

template <typename Options>
void ParseCommandLine(const std::vector<OptionSpec<Options>>& specs, Options& out) {
    std::vector<BoundOption> bound;
    bound.reserve(specs.size());
    for (const auto& spec : specs) {
        auto apply = [&spec, &out](std::wstring_view value) {
            spec.apply(out, value);
        };
        bound.push_back({.name = spec.name, .takesValue = spec.TakesValue(), .apply = std::move(apply)});
    }
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

}  // namespace d2bs::config
