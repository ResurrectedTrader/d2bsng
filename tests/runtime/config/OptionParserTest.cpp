#include <doctest/doctest.h>

#include <array>
#include <cstdint>
#include <initializer_list>
#include <ostream>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "config/OptionParser.h"

namespace {

using d2bs::core::config::OptionName;
using d2bs::core::config::OptionSpec;
using d2bs::core::config::ParseArguments;
using d2bs::core::config::ParseError;
using d2bs::core::config::ParseSettings;
using d2bs::core::config::RemoveOptions;
using d2bs::core::config::SpecBuilder;

// RemoveOptions edits in place and reports the length that is left, so a test
// gives it a mutable copy and reads back exactly that many characters.
std::wstring Remove(std::wstring_view input, std::span<const OptionName> options) {
    std::wstring buffer{input};
    const size_t remaining = RemoveOptions(std::span<wchar_t>{buffer}, options);
    REQUIRE(remaining <= buffer.size());
    // Everything past the reported length must be zeroed, never left behind.
    for (size_t i = remaining; i < buffer.size(); ++i) {
        REQUIRE(buffer[i] == L'\0');
    }
    buffer.resize(remaining);
    return buffer;
}

// A table shaped like a backend's: two value options and a bare flag.
constexpr std::array<OptionName, 3> TABLE = {
    OptionName{.name = "-token", .takesValue = true},
    OptionName{.name = "-profile", .takesValue = true},
    OptionName{.name = "-multi", .takesValue = false},
};

constexpr std::array<OptionName, 1> TOKEN = {OptionName{.name = "-token", .takesValue = true}};

}  // namespace

TEST_CASE("RemoveOptions takes a value option and its value") {
    CHECK(Remove(LR"(game.exe -w -token secret -ns)", TOKEN) == LR"(game.exe -w -ns)");
}

TEST_CASE("RemoveOptions takes a bare flag alone") {
    CHECK(Remove(LR"(game.exe -w -multi -ns)", TABLE) == LR"(game.exe -w -ns)");
}

TEST_CASE("RemoveOptions leaves the arguments the game was given") {
    CHECK(Remove(LR"(game.exe -w -token t -profile bob -multi -ns -direct)", TABLE) == LR"(game.exe -w -ns -direct)");
}

TEST_CASE("RemoveOptions leaves a command line holding none of them alone") {
    const std::wstring_view input = LR"(game.exe -w -ns)";
    CHECK(Remove(input, TABLE) == input);
}

TEST_CASE("RemoveOptions leaves nothing of the value, not even its length") {
    const std::wstring shortValue = Remove(LR"(game.exe -token a -w)", TOKEN);
    const std::wstring longValue = Remove(LR"(game.exe -token aaaaaaaaaaaaaaaaaaaaaaaaaaaaa -w)", TOKEN);
    CHECK(shortValue == longValue);
    CHECK(shortValue == LR"(game.exe -w)");
}

TEST_CASE("RemoveOptions takes a quoted value whole") {
    CHECK(Remove(LR"(game.exe -token "a b c" -w)", TOKEN) == LR"(game.exe -w)");
}

TEST_CASE("RemoveOptions takes a value holding an escaped quote") {
    CHECK(Remove(LR"(game.exe -token "a\"b c" -w)", TOKEN) == LR"(game.exe -w)");
}

TEST_CASE("RemoveOptions keeps a quoted program name intact") {
    CHECK(Remove(LR"("C:\Program Files\game.exe" -token t -w)", TOKEN) == LR"("C:\Program Files\game.exe" -w)");
}

TEST_CASE("RemoveOptions closes the gap rather than leaving a double space") {
    CHECK(Remove(LR"(game.exe -token t -w)", TOKEN) == LR"(game.exe -w)");
}

TEST_CASE("RemoveOptions takes the leading space when the pair ends the line") {
    CHECK(Remove(LR"(game.exe -w -token secret)", TOKEN) == LR"(game.exe -w)");
}

TEST_CASE("RemoveOptions takes every occurrence of a repeated option") {
    CHECK(Remove(LR"(game.exe -token a -w -token b)", TOKEN) == LR"(game.exe -w)");
}

TEST_CASE("RemoveOptions takes a trailing value option that has no value") {
    // The parser ignores a value option with nothing after it; the switch still
    // goes, because it is ours and says so.
    CHECK(Remove(LR"(game.exe -w -token)", TOKEN) == LR"(game.exe -w)");
}

TEST_CASE("RemoveOptions does not treat the program name as an option") {
    CHECK(Remove(LR"(-token secret -w)", TOKEN) == LR"(-token secret -w)");
}

TEST_CASE("RemoveOptions handles an empty command line") {
    CHECK(Remove(L"", TABLE).empty());
}

TEST_CASE("RemoveOptions handles a command line that is only the program name") {
    CHECK(Remove(LR"(game.exe)", TABLE) == LR"(game.exe)");
}

TEST_CASE("RemoveOptions with an empty table is a no-op") {
    const std::wstring_view input = LR"(game.exe -token secret)";
    CHECK(Remove(input, std::span<const OptionName>{}) == input);
}

// The remover matches the unescaped argument, the same thing the parser matched,
// and cuts the raw span it came from. Matching raw text instead silently fails
// on a quoted switch and, worse, matches a switch the parser had already eaten
// as another option's value - taking a real game argument with it.

TEST_CASE("RemoveOptions recognises a quoted switch") {
    CHECK(Remove(LR"(game.exe "-token" secret -w)", TOKEN) == LR"(game.exe -w)");
}

TEST_CASE("RemoveOptions recognises a fully quoted command line") {
    CHECK(Remove(LR"("game.exe" "-token" "secret" "-w")", TOKEN) == LR"("game.exe" "-w")");
}

TEST_CASE("RemoveOptions does not cut a switch the parser consumed as a value") {
    // -title takes a value, so the parser reads -profile as that value and never
    // sees it as a switch. The remover must agree, and leave -w alone.
    constexpr std::array<OptionName, 2> TABLE_TITLE = {
        OptionName{.name = "-title", .takesValue = true},
        OptionName{.name = "-profile", .takesValue = true},
    };
    CHECK(Remove(LR"(game.exe "-title" -profile -w)", TABLE_TITLE) == LR"(game.exe -w)");
}

TEST_CASE("RemoveOptions leaves a line alone when the splits disagree") {
    // A leading space gives CommandLineToArgvW an empty argv[0]; our split does
    // not produce one. Rather than guess, the whole line is left untouched.
    const std::wstring_view input = LR"( game.exe -token secret -w)";
    CHECK(Remove(input, TOKEN) == input);
}

TEST_CASE("RemoveOptions cuts a value that ends in a backslash") {
    // CommandLineToArgvW reads "C:\bots\" as one argument whose value ends in a
    // quote-escaping backslash, so -ns is NOT part of it and must survive.
    CHECK(Remove(LR"(game.exe -token "C:\bots\\" -ns)", TOKEN) == LR"(game.exe -ns)");
}

TEST_CASE("RemoveOptions ignores a switch spelled inside the program name") {
    CHECK(Remove(LR"("C:\-token dir\game.exe" -w)", TOKEN) == LR"("C:\-token dir\game.exe" -w)");
}

TEST_CASE("RemoveOptions separates tokens on tabs") {
    CHECK(Remove(L"game.exe\t-token\tsecret\t-w", TOKEN) == L"game.exe\t-w");
}

namespace {

// Options shaped like a backend's: a value option, a validated value option and a flag.
struct TestOptions {
    std::wstring title;
    int32_t level = 0;
    bool isMulti = false;
    int32_t titleCalls = 0;
};

std::vector<OptionSpec<TestOptions>> TestSpecs() {
    SpecBuilder<TestOptions> builder;
    builder.Add(
        "-title", "<title>", {.description = "The title.", .category = "Window", .defaultValue = "Diablo II"},
        +[](TestOptions& o, std::wstring_view v) {
            o.title = v;
            ++o.titleCalls;
        });
    builder.Add(
        "-level", "<1-99>", {.description = "The level."}, +[](TestOptions& o, std::wstring_view v) {
            if (v.size() > 2 || v.find_first_not_of(L"0123456789") != std::wstring_view::npos) {
                return false;
            }
            o.level = std::stoi(std::wstring{v});
            return o.level >= 1 && o.level <= 99;
        });
    builder.Add("-multi", "", +[](TestOptions& o, std::wstring_view) { o.isMulti = true; });
    return std::move(builder.specs);
}

std::vector<ParseError> Parse(std::initializer_list<std::wstring_view> args, TestOptions& out,
                              ParseSettings settings = {}) {
    const std::vector<std::wstring_view> list{args};
    return ParseArguments(std::span<const std::wstring_view>{list}, TestSpecs(), out, settings);
}

}  // namespace

TEST_CASE("OptionSpec carries the builder's help text") {
    const auto specs = TestSpecs();
    REQUIRE(specs.size() == 3);
    CHECK(specs[0].name == "-title");
    CHECK(specs[0].syntax == "<title>");
    CHECK(specs[0].description == "The title.");
    CHECK(specs[0].category == "Window");
    CHECK(specs[0].defaultValue == "Diablo II");
    CHECK(specs[0].TakesValue());
    CHECK(specs[2].description.empty());
    CHECK_FALSE(specs[2].TakesValue());
}

TEST_CASE("ParseArguments applies values and flags") {
    TestOptions o;
    CHECK(Parse({L"-title", L"Bot 1", L"-multi", L"-level", L"42"}, o).empty());
    CHECK(o.title == L"Bot 1");
    CHECK(o.isMulti);
    CHECK(o.level == 42);
}

TEST_CASE("ParseArguments ignores unknown switches by default") {
    TestOptions o;
    CHECK(Parse({L"-w", L"-title", L"t", L"-ns"}, o).empty());
    CHECK(o.title == L"t");
}

TEST_CASE("ParseArguments reports unknown switches when asked") {
    TestOptions o;
    const auto errors = Parse({L"-w", L"-multi"}, o, {.isUnknownAnError = true});
    REQUIRE(errors.size() == 1);
    CHECK(errors[0].option == "-w");
    CHECK(errors[0].message == "unknown switch -w");
    CHECK(o.isMulti);
}

TEST_CASE("ParseArguments reports a value option with nothing after it") {
    TestOptions o;
    const auto errors = Parse({L"-multi", L"-title"}, o);
    REQUIRE(errors.size() == 1);
    CHECK(errors[0].option == "-title");
    CHECK(errors[0].message == "-title needs a value");
    CHECK(o.titleCalls == 0);
}

TEST_CASE("ParseArguments reports a rejected value and carries on") {
    TestOptions o;
    const auto errors = Parse({L"-level", L"100", L"-multi"}, o);
    REQUIRE(errors.size() == 1);
    CHECK(errors[0].option == "-level");
    CHECK(errors[0].value == "100");
    CHECK(errors[0].message == "bad -level '100'");
    CHECK(o.isMulti);
}

TEST_CASE("ParseArguments skips an empty value without calling the handler") {
    TestOptions o;
    CHECK(Parse({L"-title", L"", L"-multi"}, o).empty());
    CHECK(o.titleCalls == 0);
    CHECK(o.isMulti);
}

TEST_CASE("ParseArguments takes a switch-looking value as the value") {
    TestOptions o;
    CHECK(Parse({L"-title", L"-multi"}, o).empty());
    CHECK(o.title == L"-multi");
    CHECK_FALSE(o.isMulti);
}

TEST_CASE("ParseArguments reports a non-ASCII value as UTF-8") {
    TestOptions o;
    const auto errors = Parse({L"-level", L"\u00e9"}, o);
    REQUIRE(errors.size() == 1);
    CHECK(errors[0].value == "\xc3\xa9");
}