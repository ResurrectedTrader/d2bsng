#pragma once

// d2bsng version string. Normally injected by the build (build.ps1 -> MSBuild
// -p:D2bsVersion -> /D D2BS_VERSION="X.Y.Z"); the #ifndef fallback covers
// un-parameterized IDE builds. d2bsng versions start at 2.x so scripts can tell
// it apart from legacy d2bs, which topped out at 1.6.x.
#ifndef D2BS_VERSION
    #define D2BS_VERSION "2.0.0-dev"
#endif
