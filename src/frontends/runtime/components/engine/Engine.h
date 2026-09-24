#pragma once

#include "unibind/unibind.h"

// The one engine platform the process runs every script isolate on. Which engine that is was
// decided at the glue link, by which unibind backend it linked; nothing here names it.
class Engine {
   public:
    // Brings the engine up on first call - with the INI's EngineFlags and thread-pool settings, and a
    // fault handler that writes a crash log - and never takes it down. See Engine.cpp for why.
    static ub::Platform& GetPlatform();

    Engine() = delete;
};
