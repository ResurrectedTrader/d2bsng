#pragma once

#include <thread>

namespace d2bs::script {

// Report a reference released off its owning thread. Out of line because the
// logger reaches spdlog, and a header the event types include must not.
void ReportOffThreadRelease();

// A value a script handed to native code, or one native code made for it, held
// on that script's behalf - a callback to invoke later, or an object a binding
// will hand back a second time.
//
// Opaque rather than typed, because this is the one piece of script state the
// runtime genuinely keeps between calls, and what keeping it costs differs per
// engine. V8 would allow a plain copyable handle; SpiderMonkey roots by address
// in an intrusive list, so the rooted thing must never move. Holding a pointer
// to frontend-owned storage satisfies both: a move transfers the pointer, and
// the rooted object stays where it is.
//
// The rules are the stricter engine's, not V8's: created, reset and destroyed
// on its owning script's thread and nowhere else, and move-only, because a copy
// would be a second GC root.
class Ref {
   public:
    Ref() = default;

    // Adopts frontend-owned state. Only a frontend calls this; a binding
    // receives a Ref from its arguments and never builds one.
    explicit Ref(void* state) : state_(state), owner_(std::this_thread::get_id()) {}

    Ref(const Ref&) = delete;
    Ref& operator=(const Ref&) = delete;

    Ref(Ref&& other) noexcept : state_(other.state_), owner_(other.owner_) { other.state_ = nullptr; }

    Ref& operator=(Ref&& other) noexcept {
        if (this != &other) {
            Reset();
            state_ = other.state_;
            owner_ = other.owner_;
            other.state_ = nullptr;
        }
        return *this;
    }

    ~Ref() { Reset(); }

    [[nodiscard]] bool IsEmpty() const { return state_ == nullptr; }

    // Whether two references name the same function. removeEventListener needs
    // it, and function identity is the engine's answer to give, not ours.
    [[nodiscard]] bool SameFunction(const Ref& other) const;

    // Drop the reference. Owning thread only, as for the destructor.
    //
    // Logged rather than asserted, and it does not stop the release: shipping
    // builds define NDEBUG, so an assert here is only ever seen by a build
    // nobody runs. V8 tolerates the release, so continuing is what already
    // happens; the report exists because a stricter engine would corrupt its
    // root list here, and this is the only warning of it anyone gets.
    void Reset() {
        if (state_ == nullptr) {
            return;
        }
        if (owner_ != std::this_thread::get_id()) {
            ReportOffThreadRelease();
        }
        Release(state_);
        state_ = nullptr;
    }

    // For the frontend that defines the operations above. Not for bindings.
    [[nodiscard]] void* State() const { return state_; }

   private:
    // Frees whatever state_ points at. Defined by the frontend.
    static void Release(void* state);

    void* state_ = nullptr;
    std::thread::id owner_;
};

}  // namespace d2bs::script
