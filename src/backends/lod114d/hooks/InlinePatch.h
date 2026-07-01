#pragma once

#include <cstdint>

// Inline JMP / CALL patcher for mid-function patch points the game's
// internals expose. Reference's `Patch.h::PatchHook[]` is the model; the
// `__declspec(naked)` intercepts that get JMP'd to live in
// src/backends/lod114d/hooks/Intercepts.cpp.

namespace d2bs::hooks {

// Write a relative CALL/JMP at site to target. Sites longer than 5 bytes get the 5-byte instruction followed by (len -
// 5) NOPs. originalOut must be at least len bytes; len must be >= 5.
void WriteCallN(uintptr_t site, uintptr_t target, size_t len, uint8_t* originalOut);
void WriteJmpN(uintptr_t site, uintptr_t target, size_t len, uint8_t* originalOut);
void RestoreN(uintptr_t site, const uint8_t* original, size_t len);

// Single-byte poke (e.g. 0xC3 RET for the cursor-lock-style "neuter the
// function" patch). Saves the original byte so callers can restore it.
void WriteByte(uintptr_t site, uint8_t value, uint8_t& originalOut);
void RestoreByte(uintptr_t site, uint8_t value);

}  // namespace d2bs::hooks
