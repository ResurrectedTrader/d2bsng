#pragma once

// The state behind the handles this package hands around. Declared here and
// deliberately never defined: what one of these is differs per frontend, only
// the frontend that made it can read it, and nothing in this package needs to.
//
// Five distinct types rather than one void*, because they are not
// interchangeable. The frame of one call, a value held past that call, and the
// record of a declared class are different things, and handing one where
// another belongs is a mistake the compiler should refuse rather than a
// convention every frontend has to keep. It costs the frontend nothing either:
// what it would have cast, it now dereferences.
//
// A native is the deliberate exception and stays void* wherever it appears - it
// is whatever the runtime made, and the contract must not know its type. That
// is the boundary this package exists to draw, and naming the pointer would
// document it without making anything safer.
namespace d2bs::script {

// One call into a binding, for as long as that call lasts: its receiver, its
// arguments, its return slot, and every value it has read or built.
struct CallState;

// One value held past the end of the call that produced it - a callback to
// invoke later, or an object a binding will hand back a second time.
struct PersistentState;

// One bound class, as the frontend recorded it when the runtime declared it.
// Process-wide, which is what lets a ClassKey name one.
struct ClassRecord;

// One named object with properties but no class of its own, likewise.
struct ObjectRecord;

// What a registration pass registers into, for a frontend that registers into
// something rather than into declarations of its own.
struct RegistryState;

}  // namespace d2bs::script
