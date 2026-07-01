#include "JSFile.h"

#include <cstdio>
#include <string>

#include <fmt/format.h>

#include "api/core/V8Convert.h"
#include "api/core/V8Error.h"
#include "config/AppConfig.h"

namespace d2bs::api::classes::file_detail {

FILE* FileOpenRelScript(v8::Isolate* isolate, const std::string& relativePath, const wchar_t* mode) {
    auto fullPath = config::GetPathRelScript(relativePath);
    if (fullPath.empty()) {
        v8_error::ThrowError(isolate, "Invalid file name");
        return nullptr;
    }
    FILE* fp = nullptr;
    if (_wfopen_s(&fp, fullPath.c_str(), mode) != 0 || fp == nullptr) {
        v8_error::ThrowError(isolate, "Couldn't open file");
        return nullptr;
    }
    return fp;
}

std::string ReadLine(FILE* fptr) {
    if (feof(fptr)) {
        return "";
    }
    std::string buffer;
    while (true) {
        int32_t ch = fgetc(fptr);
        if (feof(fptr) || ferror(fptr)) {
            break;
        }
        if (ch == '\n') {
            break;
        }
        if (ch != '\r') {
            buffer += static_cast<char>(ch);
        }
    }
    return buffer;
}

bool WriteValue(FILE* fptr, v8::Isolate* isolate, v8::Local<v8::Value> value, bool isBinary) {
    if (value->IsNullOrUndefined()) {
        int32_t zero = 0;
        return fwrite(&zero, sizeof(int32_t), 1, fptr) == 1;
    }

    if (value->IsString()) {
        std::string str = v8_convert::ToString(isolate, value);
        return fwrite(str.data(), sizeof(char), str.size(), fptr) == str.size();
    }

    if (value->IsNumber()) {
        auto context = isolate->GetCurrentContext();
        if (isBinary) {
            if (value->IsInt32()) {
                int32_t ival = value->Int32Value(context).FromMaybe(0);
                return fwrite(&ival, sizeof(int32_t), 1, fptr) == 1;
            }
            double dval = value->NumberValue(context).FromMaybe(0.0);
            return fwrite(&dval, sizeof(double), 1, fptr) == 1;
        }
        // Text mode
        if (value->IsInt32()) {
            int32_t ival = value->Int32Value(context).FromMaybe(0);
            std::string str = fmt::format("{}", ival);
            return fwrite(str.data(), sizeof(char), str.size(), fptr) == str.size();
        }
        double dval = value->NumberValue(context).FromMaybe(0.0);
        std::string str = fmt::format("{:.16f}", dval);
        return fwrite(str.data(), sizeof(char), str.size(), fptr) == str.size();
    }

    if (value->IsBoolean()) {
        bool bval = value->BooleanValue(isolate);
        if (isBinary) {
            return fwrite(&bval, sizeof(bool), 1, fptr) == 1;
        }
        const char* str = bval ? "true" : "false";
        size_t len = strlen(str);
        return fwrite(str, sizeof(char), len, fptr) == len;
    }

    return false;
}

size_t SkipBom(const char* data, size_t size) {
    if (size >= 3 && data[0] == '\xEF' && data[1] == '\xBB' && data[2] == '\xBF') {
        return 3;
    }
    return 0;
}

}  // namespace d2bs::api::classes::file_detail
