#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace d2bs::imports {

// ----------------------------------------------------------------------------
// Calling-convention tag types
// ----------------------------------------------------------------------------
// Win32 x86 has three distinct ABIs (stdcall, fastcall, cdecl). The function
// pointer type encodes the convention, so a wrong tag at the call site is a
// compile-time error rather than a silent ABI corruption.

struct CcStdcall {};
struct CcFastcall {};
struct CcCdecl {};

// ----------------------------------------------------------------------------
// IImport - base for everything the registry holds
// ----------------------------------------------------------------------------

class IImport {
   public:
    IImport() noexcept = default;
    IImport(const IImport&) = delete;
    IImport(IImport&&) = delete;
    IImport& operator=(const IImport&) = delete;
    IImport& operator=(IImport&&) = delete;
    virtual ~IImport() = default;

    // Resolve the import against the loaded module's base address. Called once,
    // from Bridge::Init(), after every static initializer has registered.
    virtual void Resolve(uintptr_t base) noexcept = 0;
};

// ----------------------------------------------------------------------------
// Registry - Meyers singleton that tracks every IImport instance
// ----------------------------------------------------------------------------

class Registry {
   public:
    static Registry& Get() noexcept;

    void Register(IImport* item) noexcept;
    void ResolveAll(uintptr_t base) noexcept;

    Registry(const Registry&) = delete;
    Registry(Registry&&) = delete;
    Registry& operator=(const Registry&) = delete;
    Registry& operator=(Registry&&) = delete;

   private:
    Registry() = default;
    ~Registry() = default;

    std::vector<IImport*> items_;
};

// ----------------------------------------------------------------------------
// GameFunc<Cc, Sig> - typed wrapper around an offset-into-module function
// ----------------------------------------------------------------------------
// Primary template is left undefined; only the three CC specialisations below
// are usable. Picking the wrong tag for a function instantiates the primary
// and produces a compile error.

template <typename Cc, typename Sig>
class GameFunc;

#define D2BS_IMPORTS_DEFINE_GAMEFUNC(CC_TAG, CC_KEYWORD)                          \
    template <typename Ret, typename... Args>                                     \
    class GameFunc<CC_TAG, Ret(Args...)> : public IImport {                       \
       public:                                                                    \
        using FnPtr = Ret(CC_KEYWORD*)(Args...);                                  \
                                                                                  \
        constexpr explicit GameFunc(uint32_t offset) noexcept : offset_(offset) { \
            Registry::Get().Register(this);                                       \
        }                                                                         \
                                                                                  \
        void Resolve(uintptr_t base) noexcept override {                          \
            ptr_ = reinterpret_cast<FnPtr>(base + offset_);                       \
        }                                                                         \
                                                                                  \
        Ret operator()(Args... args) const {                                      \
            return ptr_(args...);                                                 \
        }                                                                         \
                                                                                  \
        [[nodiscard]] FnPtr Ptr() const noexcept {                                \
            return ptr_;                                                          \
        }                                                                         \
        [[nodiscard]] bool IsResolved() const noexcept {                          \
            return ptr_ != nullptr;                                               \
        }                                                                         \
        [[nodiscard]] uint32_t Offset() const noexcept {                          \
            return offset_;                                                       \
        }                                                                         \
                                                                                  \
       private:                                                                   \
        uint32_t offset_;                                                         \
        FnPtr ptr_ = nullptr;                                                     \
    };

D2BS_IMPORTS_DEFINE_GAMEFUNC(CcStdcall, __stdcall)
D2BS_IMPORTS_DEFINE_GAMEFUNC(CcFastcall, __fastcall)
D2BS_IMPORTS_DEFINE_GAMEFUNC(CcCdecl, __cdecl)

#undef D2BS_IMPORTS_DEFINE_GAMEFUNC

// Convenience aliases so per-DLL headers don't have to spell out the CC tag.
template <typename Sig>
using StdcallFunc = GameFunc<CcStdcall, Sig>;

template <typename Sig>
using FastcallFunc = GameFunc<CcFastcall, Sig>;

template <typename Sig>
using CdeclFunc = GameFunc<CcCdecl, Sig>;

// ----------------------------------------------------------------------------
// GameVar<T> - typed wrapper around an offset-into-module variable
// ----------------------------------------------------------------------------

template <typename T>
class GameVar : public IImport {
   public:
    constexpr explicit GameVar(uint32_t offset) noexcept : offset_(offset) { Registry::Get().Register(this); }

    void Resolve(uintptr_t base) noexcept override { ptr_ = reinterpret_cast<T*>(base + offset_); }

    [[nodiscard]] T* Ptr() const noexcept { return ptr_; }
    [[nodiscard]] T& operator*() const noexcept { return *ptr_; }
    [[nodiscard]] T* operator->() const noexcept { return ptr_; }
    [[nodiscard]] bool IsResolved() const noexcept { return ptr_ != nullptr; }
    [[nodiscard]] uint32_t Offset() const noexcept { return offset_; }

   private:
    uint32_t offset_;
    T* ptr_ = nullptr;
};

// ----------------------------------------------------------------------------
// GameAsmFunc - raw address wrapper for interior entry points
// ----------------------------------------------------------------------------
// Used for hand-written naked thunk sites: the offset names a target the
// asm-thunk module reads via .Addr() during its Init(). Each call site should
// carry its own justification for needing the raw address.

class GameAsmFunc : public IImport {
   public:
    constexpr explicit GameAsmFunc(uint32_t offset) noexcept : offset_(offset) { Registry::Get().Register(this); }

    void Resolve(uintptr_t base) noexcept override { addr_ = base + offset_; }

    [[nodiscard]] uintptr_t Addr() const noexcept { return addr_; }
    [[nodiscard]] bool IsResolved() const noexcept { return addr_ != 0; }
    [[nodiscard]] uint32_t Offset() const noexcept { return offset_; }

   private:
    uint32_t offset_;
    uintptr_t addr_ = 0;
};

}  // namespace d2bs::imports
