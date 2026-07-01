#pragma once

// D2MOO version selection - must precede any D2MOO header inclusion.
// D2MOO's D2BuildInformation.h falls back to 1.10f when nothing is predefined,
// so every TU that pulls in a D2MOO header MUST go through this config first
// (or rely on the matching project-wide PreprocessorDefinitions in d2bs.vcxproj).

// Diablo II 1.14d: monolithic Game.exe, expansion (LoD), the only target
// this codebase supports today.
#ifndef D2_VERSION_114D
    #define D2_VERSION_114D
#endif
#ifndef D2_VERSION_MAJOR
    #define D2_VERSION_MAJOR 1
#endif
#ifndef D2_VERSION_MINOR
    #define D2_VERSION_MINOR 14
#endif
#ifndef D2_VERSION_PATCH
    #define D2_VERSION_PATCH 'D'
#endif

// 1.14+ collapsed every former DLL into Game.exe. D2MOO uses this to switch
// off DLL bookkeeping (DllBases, GetProcAddress lookups, ...).
#ifndef D2_IS_MONOLITHIC
    #define D2_IS_MONOLITHIC TRUE
#endif

// Lord of Destruction features (act 5, expansion stats, ladder runewords, ...).
#ifndef D2_VERSION_EXPANSION
    #define D2_VERSION_EXPANSION 1
#endif

// Pandemonium Event / Uber Tristram. Predicate-style macro: D2MOO checks
// `#ifdef D2_VERSION_HAS_UBERS`, never inspects its value.
#ifndef D2_VERSION_HAS_UBERS
    #define D2_VERSION_HAS_UBERS
#endif

// Per-DLL declspec-import shim macros. D2MOO normally sets these from CMake
// (TargetsHelpers.cmake's D2MOO_set_dll_decl): empty when building a static
// library / consuming as part of the same module, `__declspec(dllimport)`
// when linking against a DLL. We never link against any D2MOO output -
// every former-DLL function is resolved via GetModuleHandle(nullptr) +
// offset against Game.exe - so each macro is empty here. Overridable via
// project PreprocessorDefinitions.
#ifndef D2COMMON_DLL_DECL
    #define D2COMMON_DLL_DECL
#endif
#ifndef D2GAME_DLL_DECL
    #define D2GAME_DLL_DECL
#endif
#ifndef D2GFX_DLL_DECL
    #define D2GFX_DLL_DECL
#endif
#ifndef D2WIN_DLL_DECL
    #define D2WIN_DLL_DECL
#endif
#ifndef D2NET_DLL_DECL
    #define D2NET_DLL_DECL
#endif
#ifndef D2LANG_DLL_DECL
    #define D2LANG_DLL_DECL
#endif
#ifndef D2MCPCLIENT_DLL_DECL
    #define D2MCPCLIENT_DLL_DECL
#endif
#ifndef D2DEBUGGER_DLL_DECL
    #define D2DEBUGGER_DLL_DECL
#endif
#ifndef FOG_DLL_DECL
    #define FOG_DLL_DECL
#endif
#ifndef STORM_DLL_DECL
    #define STORM_DLL_DECL
#endif
