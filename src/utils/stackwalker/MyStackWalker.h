#pragma once

#include <filesystem>
#include <map>
#include <sstream>
#include <string>

#include <StackWalker.h>

class MyStackWalker : StackWalker {
    std::map<std::string, uint64_t> baseAddresses_;
    uint32_t frameIndex_ = 0;
    uint32_t skip_ = 0;
    std::stringstream output_;

    void OnSymInit(LPCSTR szSearchPath, DWORD symOptions, LPCSTR szUserName) override {}

    void OnLoadModule(LPCSTR img, LPCSTR mod, DWORD64 baseAddr, DWORD size, DWORD result, LPCSTR symType,
                      LPCSTR pdbName, ULONGLONG fileVersion) override {
        auto module = std::string(mod);
        module = module.substr(0, module.find('.'));
        baseAddresses_.insert_or_assign(module, baseAddr);
    }

    void OnDbgHelpErr(LPCSTR szFuncName, DWORD gle, DWORD64 addr) override {}

    void OnCallstackEntry(CallstackEntryType eType, CallstackEntry &entry) override {
        if (eType == firstEntry) {
            frameIndex_ = 0;
        } else {
            frameIndex_++;
        }
        if (skip_ > 0) {
            skip_--;
            return;
        }

        if (eType == lastEntry || entry.offset == 0)
            return;

        uint64_t relativeAddress = UINT64_MAX;
        const std::string module(entry.moduleName);
        const auto baseAddressEntry = baseAddresses_.find(module);
        if (baseAddressEntry != baseAddresses_.end()) {
            relativeAddress = entry.offset - (*baseAddressEntry).second;
        }

        if (entry.undFullName[0])
            strncpy_s(entry.name, entry.undFullName, STACKWALK_MAX_NAMELEN);
        else if (entry.undName[0])
            strncpy_s(entry.name, entry.undName, STACKWALK_MAX_NAMELEN);

        if (!entry.moduleName[0]) {
            strcpy_s(entry.moduleName, "<unknown module>");
        }

        if (!entry.name[0]) {
            if (relativeAddress != UINT64_MAX) {
                strcpy_s(entry.name, std::format("+{:#x}", relativeAddress).c_str());
            } else {
                strcpy_s(entry.name, std::format("{:#x}", entry.offset).c_str());
            }
        } else {
            if (relativeAddress != UINT64_MAX) {
                strcpy_s(entry.moduleName, std::format("{}+{:#x}", entry.moduleName, relativeAddress).c_str());
            } else {
                strcpy_s(entry.moduleName, std::format("{} ({:#x})", entry.moduleName, entry.offset).c_str());
            }
        }

        if (!entry.lineFileName[0]) {
            output_ << std::format("{:>4} # {} in {}", frameIndex_, entry.name, entry.moduleName) << '\n';
        } else {
            output_ << std::format("{:>4} # {} at {}:{}", frameIndex_, entry.name,
                                   std::filesystem::path(entry.lineFileName).filename().string(), entry.lineNumber)
                    << '\n';
        }
    }

   public:
    std::string GetStackTrace(HANDLE thread = GetCurrentThread(), uint32_t skip = 0, const CONTEXT *context = nullptr) {
        frameIndex_ = 0;
        skip_ = skip;
        output_.str("");
        if (context != nullptr) {
            // ShowCallstack copies the context internally, so the const_cast
            // is safe - StackWalker just declares it non-const.
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast)
            ShowCallstack(thread, const_cast<CONTEXT *>(context));
        } else {
            ShowCallstack(thread);
        }
        return output_.str();
    }
};
