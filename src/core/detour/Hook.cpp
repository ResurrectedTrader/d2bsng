#include "Hook.h"

#include <Windows.h>
#include <detours/detours.h>

#include <algorithm>
#include <string>

namespace d2bs::detour {

namespace {

// Detours reports its own failures as Win32 error codes, so the resolve
// failures this layer adds use the matching ones.
constexpr int32_t MODULE_NOT_LOADED_ERROR = ERROR_MOD_NOT_FOUND;
constexpr int32_t TARGET_MISSING_ERROR = ERROR_PROC_NOT_FOUND;

// GetModuleHandleW only: a slot may be attached under the loader lock, where
// loading a module is not allowed. Reporting the miss and letting a later
// attach pick the module up is the contract.
void* ResolveExport(std::wstring_view module, std::string_view name, AttachResult& outcome) {
    const HMODULE handle = GetModuleHandleW(std::wstring{module}.c_str());
    if (handle == nullptr) {
        outcome = AttachResult::ModuleNotLoaded;
        return nullptr;
    }
    // NOLINTNEXTLINE(clang-diagnostic-cast-function-type-strict) - GetProcAddress returns FARPROC
    auto* proc = reinterpret_cast<void*>(GetProcAddress(handle, std::string{name}.c_str()));
    if (proc == nullptr) {
        outcome = AttachResult::TargetMissing;
    }
    return proc;
}

int32_t ErrorFor(AttachResult result, int32_t batchError) {
    switch (result) {
        case AttachResult::ModuleNotLoaded:
            return MODULE_NOT_LOADED_ERROR;
        case AttachResult::TargetMissing:
            return TARGET_MISSING_ERROR;
        case AttachResult::Failed:
            return batchError != 0 ? batchError : TARGET_MISSING_ERROR;
        default:
            return 0;
    }
}

int32_t BeginTransaction() {
    if (const LONG err = DetourTransactionBegin(); err != NO_ERROR) {
        // Detours has two failure modes here and they need opposite handling.
        // One leaves no transaction open; the other - failing to make the
        // trampoline regions writable - has already claimed the process-wide
        // pending-transaction slot for this thread and returns the error
        // anyway. Leaving that claimed wedges Detours for the whole process:
        // every later Begin, on any thread, fails with ERROR_INVALID_OPERATION,
        // so no hook can be installed OR removed for the rest of the run.
        // Abort is itself a no-op unless this thread holds the slot, so it is
        // safe for both modes.
        DetourTransactionAbort();
        return err;
    }
    DetourUpdateThread(GetCurrentThread());
    return 0;
}

}  // namespace

Batch::Batch() : error_(BeginTransaction()), isOpen_(error_ == 0) {}

Batch::~Batch() {
    Abort();
}

void Batch::RecordBody(const void* body) {
    if (body != nullptr) {
        bodies_.push_back(body);
    }
}

bool Batch::HasBody(const void* body) const {
    return body != nullptr && std::ranges::find(bodies_, body) != bodies_.end();
}

AttachResult Batch::Attach(Slot& slot) {
    if (!isOpen_) {
        return AttachResult::Failed;
    }
    if (slot.attached_) {
        RecordBody(slot.body_);
        return AttachResult::AlreadyAttached;
    }

    AttachResult outcome = AttachResult::Queued;
    void* target = nullptr;
    switch (slot.target_.Which()) {
        case Target::Kind::Export:
            target = ResolveExport(slot.target_.Module(), slot.target_.Name(), outcome);
            break;
        case Target::Kind::Address:
            target = slot.target_.RawAddress();
            break;
        case Target::Kind::None:
            break;
    }
    if (target == nullptr) {
        return outcome == AttachResult::Queued ? AttachResult::TargetMissing : outcome;
    }

    // Two exports can be thunks into one implementation - advapi32 and
    // kernel32 both forward RegQueryValueExW into kernelbase - and attaching
    // both would chain the detour onto itself.
    const void* body = DetourCodeFromPointer(target, nullptr);
    if (HasBody(body)) {
        return AttachResult::DuplicateBody;
    }

    slot.real_ = target;
    slot.body_ = body;
    if (const LONG err = DetourAttach(&slot.real_, slot.replacement_); err != NO_ERROR) {
        error_ = err;
        return AttachResult::Failed;
    }
    RecordBody(body);
    attaching_.push_back(&slot);
    return AttachResult::Queued;
}

DetachResult Batch::Detach(Slot& slot) {
    if (!isOpen_ || !slot.attached_) {
        return DetachResult::NotAttached;
    }
    if (const LONG err = DetourDetach(&slot.real_, slot.replacement_); err != NO_ERROR) {
        error_ = err;
        return DetachResult::Failed;
    }
    detaching_.push_back(&slot);
    return DetachResult::Queued;
}

int32_t Batch::Commit() {
    if (!isOpen_) {
        return error_;
    }
    isOpen_ = false;
    if (attaching_.empty() && detaching_.empty()) {
        DetourTransactionAbort();
        return 0;
    }

    const LONG err = DetourTransactionCommit();
    if (err != NO_ERROR) {
        // Detours rolled every rewritten pointer back to its target, and no
        // slot outside this batch was touched, so nothing is left claiming an
        // attachment that did not happen.
        error_ = err;
        return err;
    }

    for (Slot* slot : attaching_) {
        slot->attached_ = true;
    }
    // Detours restores each detached pointer to the original function, so the
    // slot keeps a usable Real() after its detour is gone.
    for (Slot* slot : detaching_) {
        slot->attached_ = false;
    }
    return 0;
}

void Batch::Abort() {
    if (!isOpen_) {
        return;
    }
    isOpen_ = false;
    attaching_.clear();
    detaching_.clear();
    DetourTransactionAbort();
}

int32_t AttachAll(std::span<Slot* const> slots) {
    Batch batch;
    for (Slot* slot : slots) {
        const AttachResult result = batch.Attach(*slot);
        if (const int32_t err = ErrorFor(result, batch.Error()); err != 0) {
            batch.Abort();
            return err;
        }
    }
    return batch.Commit();
}

int32_t DetachAll(std::span<Slot* const> slots) {
    Batch batch;
    for (Slot* slot : slots) {
        batch.Detach(*slot);
    }
    return batch.Commit();
}

}  // namespace d2bs::detour
