#pragma once

#include <cstdint>
#include <initializer_list>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

// Typed Detours slots and the transactions that attach them. A slot owns one
// detour: where the function lives, the pointer that reaches the original once
// hooked, and whether it is currently attached. Slots are attached and detached
// through a Batch, which maps one Detours transaction, so a group of
// hooks lands all-or-nothing.
//
// Detours transactions are per-thread and do not nest: keep one batch open at a
// time on a thread, and do not hand the same slot to two batches at once.

namespace d2bs::detour {

// Where the function a detour replaces lives.
class Target {
   public:
    enum class Kind { None, Export, Address };

    Target() = default;

    // A named export of a module the caller expects to be loaded already. The
    // views must outlive the target; string literals are the expected source.
    static Target Export(std::wstring_view module, std::string_view name) noexcept {
        return Target{Kind::Export, module, name, nullptr};
    }

    // A function whose address the caller already has.
    static Target Address(void* address) noexcept { return Target{Kind::Address, {}, {}, address}; }

    [[nodiscard]] Kind Which() const noexcept { return kind_; }
    [[nodiscard]] std::wstring_view Module() const noexcept { return module_; }
    [[nodiscard]] std::string_view Name() const noexcept { return name_; }
    [[nodiscard]] void* RawAddress() const noexcept { return address_; }

   private:
    Target(Kind kind, std::wstring_view module, std::string_view name, void* address) noexcept
        : kind_(kind), module_(module), name_(name), address_(address) {}

    Kind kind_ = Kind::None;
    std::wstring_view module_;
    std::string_view name_;
    void* address_ = nullptr;
};

// What Batch::Attach did with a slot.
enum class AttachResult {
    Queued,           // added to the open transaction
    AlreadyAttached,  // the slot is already hooked; nothing queued
    DuplicateBody,    // another slot in this batch covers the same code, with the same replacement
    ModuleNotLoaded,  // the module is not in the process yet; a later attach can still succeed
    TargetMissing,    // the module exports no such name, or the address is null
    Failed,           // Detours refused the attach; Batch::Error() carries the code
};

// What Batch::Detach did with a slot.
enum class DetachResult {
    Queued,
    NotAttached,  // the slot is not attached, so there is nothing to unwind
    NotOpen,      // the batch never opened; the slot's state is unknown, not clean
    Failed,       // Detours refused the detach; Batch::Error() carries the code
};

// One detour: its target, the replacement, and the pointer Detours rewrites to
// reach the original. Hook adds the typed access; this base exists so a
// batch can hold slots of unrelated signatures.
class Slot {
   public:
    Slot(const Slot&) = delete;
    Slot& operator=(const Slot&) = delete;
    Slot(Slot&&) = delete;
    Slot& operator=(Slot&&) = delete;

    [[nodiscard]] bool IsAttached() const noexcept { return attached_; }
    [[nodiscard]] const Target& Spec() const noexcept { return target_; }

    // Point the slot at a different function. Ignored while attached, since the
    // pointer Detours rewrote is what reaches the original. Pointing a detached
    // slot at an export drops the address it had: the new target is somewhere
    // else, and where is not known until the attach resolves it.
    void SetTarget(Target target) noexcept {
        if (!attached_) {
            target_ = target;
            real_ = target.RawAddress();
        }
    }

   protected:
    Slot(Target target, void* replacement) noexcept
        : real_(target.RawAddress()), target_(target), replacement_(replacement) {}
    ~Slot() = default;

    // The original function: the trampoline while attached, otherwise the
    // target itself once it is known (an export stays null until it resolves).
    // Detours restores it to the target on detach, so a slot that was never
    // attached and one that has been detached both still reach the original.
    void* real_ = nullptr;

   private:
    friend class Batch;

    Target target_;
    void* replacement_ = nullptr;
    // The code the target resolves to, which is what de-duplicates two exports
    // that are thunks into one implementation.
    const void* body_ = nullptr;
    bool attached_ = false;
};

// A detour slot for a function of type Fn. Real() reaches the original whether
// or not the slot is attached, so a caller outside the replacement can use it
// as a plain "call the real thing" handle - once the slot knows where the
// original is. A slot built from an address knows immediately; one built from
// an export name, or from the replacement alone, does not until it is attached
// or handed an address, and Real() is null until then.
template <typename Fn>
class Hook : public Slot {
   public:
    // Detour a function whose address the caller already has. Typed, so the
    // target and the replacement are checked against each other.
    Hook(Fn target, Fn replacement) noexcept
        : Slot(Target::Address(reinterpret_cast<void*>(target)), reinterpret_cast<void*>(replacement)) {}

    // Detour a target resolved when the slot is attached.
    Hook(Target target, Fn replacement) noexcept : Slot(target, reinterpret_cast<void*>(replacement)) {}

    // A slot whose target is only known later, through SetTarget.
    explicit Hook(Fn replacement) noexcept : Slot(Target{}, reinterpret_cast<void*>(replacement)) {}

    using Slot::SetTarget;
    void SetTarget(Fn target) noexcept { SetTarget(Target::Address(reinterpret_cast<void*>(target))); }

    // The original function.
    [[nodiscard]] Fn Real() const noexcept { return reinterpret_cast<Fn>(real_); }

    template <typename... Args>
    decltype(auto) operator()(Args&&... args) const {
        return Real()(std::forward<Args>(args)...);
    }
};

// One Detours transaction. Queue attaches and detaches, then commit them
// together; a batch that is destroyed without a commit aborts.
class Batch {
   public:
    Batch();
    ~Batch();

    Batch(const Batch&) = delete;
    Batch& operator=(const Batch&) = delete;
    Batch(Batch&&) = delete;
    Batch& operator=(Batch&&) = delete;

    // Resolve `slot`'s target and queue the attach. A slot that is already
    // attached, resolves to code another slot in this batch already covers, or
    // whose module is not loaded yet queues nothing and leaves the slot alone.
    AttachResult Attach(Slot& slot);

    // Queue the detach of an attached slot. A slot that is not attached queues
    // nothing, which is what keeps a failed install from being unwound.
    DetachResult Detach(Slot& slot);

    // The last error Detours reported to Attach / Detach / Commit, or 0.
    [[nodiscard]] int32_t Error() const noexcept { return error_; }

    // Commit the queued operations and mark the slots. Returns 0 on success,
    // including when nothing was queued - an empty transaction is aborted
    // rather than committed. On failure Detours has rolled everything back, so
    // no slot in this batch changes state.
    int32_t Commit();

    // Drop the transaction, leaving every slot as it was.
    void Abort();

   private:
    // One body this batch has claimed, and the replacement that claimed it.
    // Two exports sharing a body may only be de-duplicated when they detour to
    // the same replacement - otherwise calls through the skipped name would
    // enter a function with a different signature.
    struct ClaimedBody {
        const void* body = nullptr;
        const void* replacement = nullptr;
    };

    void RecordBody(const void* body, const void* replacement);
    [[nodiscard]] const ClaimedBody* FindBody(const void* body) const;

    std::vector<Slot*> attaching_;
    std::vector<Slot*> detaching_;
    std::vector<ClaimedBody> bodies_;
    int32_t error_ = 0;
    bool isOpen_ = false;
};

// Attach every slot in one all-or-nothing transaction. Returns 0 on success; a
// slot that cannot be resolved aborts the batch and yields the matching Win32
// error. Slots that are already attached, and duplicates of a body the batch
// already covers, are skipped rather than treated as failures.
int32_t AttachAll(std::span<Slot* const> slots);

// Detach every attached slot in one transaction. Slots that are not attached
// are skipped; detaching nothing is a success.
int32_t DetachAll(std::span<Slot* const> slots);

inline int32_t AttachAll(std::initializer_list<Slot*> slots) {
    return AttachAll(std::span{slots.begin(), slots.size()});
}

inline int32_t DetachAll(std::initializer_list<Slot*> slots) {
    return DetachAll(std::span{slots.begin(), slots.size()});
}

}  // namespace d2bs::detour
