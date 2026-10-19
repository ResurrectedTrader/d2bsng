#pragma once

#include <Windows.h>

namespace d2bs::utils {

class CriticalSectionLock {
    CRITICAL_SECTION& criticalSection_;

   public:
    explicit CriticalSectionLock(CRITICAL_SECTION& criticalSection) : criticalSection_(criticalSection) {
        EnterCriticalSection(&criticalSection_);
    }
    ~CriticalSectionLock() { LeaveCriticalSection(&criticalSection_); }

    CriticalSectionLock(const CriticalSectionLock&) = delete;
    CriticalSectionLock& operator=(const CriticalSectionLock&) = delete;
    CriticalSectionLock(CriticalSectionLock&&) = delete;
    CriticalSectionLock& operator=(CriticalSectionLock&&) = delete;
};

}  // namespace d2bs::utils
