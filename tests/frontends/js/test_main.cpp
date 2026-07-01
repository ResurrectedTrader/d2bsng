// This TU generates doctest's implementation and main(): the
// DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN define must come before the include, which
// then emits the test registry, runner, and entry point. The include is not
// redundant - it is the whole point of this file.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
// ReSharper disable once CppUnusedIncludeDirective
#include <doctest/doctest.h>
