#pragma once

#include <v8.h>

#include "config/ProfileData.h"

namespace d2bs::api::globals {

enum class FileMode : int32_t { Read = 0, Write = 1, Append = 2 };

using config::ProfileType;

// Register constants as global variables
void RegisterConstants(v8::Isolate* isolate, v8::Local<v8::ObjectTemplate> global);

}  // namespace d2bs::api::globals
