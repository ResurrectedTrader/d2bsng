#pragma once

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include <io.h>
#include <spdlog/spdlog.h>
#include <sys/stat.h>
#include <v8.h>

#include "api/core/V8Class.h"
#include "api/core/V8Convert.h"
#include "api/core/V8Error.h"
#include "api/globals/Constants.h"

namespace d2bs::api::classes {

using d2bs::api::globals::FileMode;

namespace file_detail {
FILE* FileOpenRelScript(v8::Isolate* isolate, const std::string& relativePath, const wchar_t* mode);
std::string ReadLine(FILE* fptr);
bool WriteValue(FILE* fptr, v8::Isolate* isolate, v8::Local<v8::Value> value, bool isBinary);
size_t SkipBom(const char* data, size_t size);
}  // namespace file_detail

// Internal file data structure
struct FileData {
    int32_t mode = 0;            // File mode (read/write/append) + binary flag (mode > 2 means binary)
    std::filesystem::path path;  // File path relative to scripts folder
    bool autoflush = false;      // Automatically flush after each write
    bool isLocked = false;       // Whether file is locked for exclusive access
    FILE* handle = nullptr;      // File pointer

    ~FileData() {
        if (handle) {
            if (isLocked) {
                _unlock_file(handle);
            }
            if (fclose(handle) != 0) {
                spdlog::warn("Close failed for file: {}", path.string());
            }
        }
    }
};

// JSFile - V8 wrapper for file operations
class JSFile : public V8ClassBase<JSFile, FileData> {
   public:
    static constexpr std::string_view ClassName = "File";

    // File objects are created via File.open(), not direct construction
    V8_CLASS_NOT_CONSTRUCTABLE

    // Mode strings indexed by mode value (0-5)
    // NOLINTBEGIN(cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays)
    static constexpr const wchar_t* MODE_STRINGS[] = {L"rt", L"w+t", L"a+t", L"rb", L"w+b", L"a+b"};
    // NOLINTEND(cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays)

    static void ConfigureTemplate(v8::Isolate* isolate, v8::Local<v8::FunctionTemplate> tpl) {
        auto inst = tpl->InstanceTemplate();
        auto proto = tpl->PrototypeTemplate();

        /// @description True while the file is open, not at end-of-file, and has no pending error.
        /// @type {boolean}
        Property(
            isolate, inst, "readable",
            +[](v8::Local<v8::Name> property, const v8::PropertyCallbackInfo<v8::Value>& info) {
                auto* isolate = info.GetIsolate();
                auto self = info.Holder();
                auto* data = Unwrap(self);

                if (!data) {
                    info.GetReturnValue().Set(v8_convert::ToV8(isolate, false));
                    return;
                }

                // File is readable if open, not at EOF, and no errors
                bool readable = data->handle && !feof(data->handle) && !ferror(data->handle);
                info.GetReturnValue().Set(v8_convert::ToV8(isolate, readable));
            });
        /// @description True while the file is open in write or append mode and has no pending error.
        /// @type {boolean}
        Property(
            isolate, inst, "writeable",
            +[](v8::Local<v8::Name> property, const v8::PropertyCallbackInfo<v8::Value>& info) {
                auto* isolate = info.GetIsolate();
                auto self = info.Holder();
                auto* data = Unwrap(self);

                if (!data) {
                    info.GetReturnValue().Set(v8_convert::ToV8(isolate, false));
                    return;
                }

                // File is writeable if open, no errors, and mode is write or append
                bool writeable =
                    data->handle && !ferror(data->handle) && (data->mode % 3) > static_cast<int32_t>(FileMode::Read);
                info.GetReturnValue().Set(v8_convert::ToV8(isolate, writeable));
            });
        /// @description True while the file is open and has no pending error.
        /// @type {boolean}
        Property(
            isolate, inst, "seekable",
            +[](v8::Local<v8::Name> property, const v8::PropertyCallbackInfo<v8::Value>& info) {
                auto* isolate = info.GetIsolate();
                auto self = info.Holder();
                auto* data = Unwrap(self);

                if (!data) {
                    info.GetReturnValue().Set(v8_convert::ToV8(isolate, false));
                    return;
                }

                bool seekable = data->handle && !ferror(data->handle);
                info.GetReturnValue().Set(v8_convert::ToV8(isolate, seekable));
            });
        /// @description Base open mode without the binary flag: 0 = read (FILE_READ), 1 = write (FILE_WRITE), 2 =
        /// append (FILE_APPEND).
        /// @type {number}
        Property(
            isolate, inst, "mode", +[](v8::Local<v8::Name> property, const v8::PropertyCallbackInfo<v8::Value>& info) {
                auto* isolate = info.GetIsolate();
                auto self = info.Holder();
                auto* data = Unwrap(self);

                if (!data) {
                    info.GetReturnValue().Set(v8_convert::ToV8(isolate, 0));
                    return;
                }

                // Return base mode (0, 1, or 2) without binary flag
                info.GetReturnValue().Set(v8_convert::ToV8(isolate, data->mode % 3));
            });
        /// @description True if the file was opened in binary mode, where read/write operate on 32-bit integers rather
        /// than text.
        /// @type {boolean}
        Property(
            isolate, inst, "binaryMode",
            +[](v8::Local<v8::Name> property, const v8::PropertyCallbackInfo<v8::Value>& info) {
                auto* isolate = info.GetIsolate();
                auto self = info.Holder();
                auto* data = Unwrap(self);

                if (!data) {
                    info.GetReturnValue().Set(v8_convert::ToV8(isolate, false));
                    return;
                }

                // Binary mode if mode > 2 (3, 4, 5 are binary versions of read, write, append)
                info.GetReturnValue().Set(v8_convert::ToV8(isolate, data->mode > 2));
            });
        /// @description Total length of the file in bytes.
        /// @type {number}
        Property(
            isolate, inst, "length",
            +[](v8::Local<v8::Name> property, const v8::PropertyCallbackInfo<v8::Value>& info) {
                auto* isolate = info.GetIsolate();
                auto self = info.Holder();
                auto* data = Unwrap(self);

                if (!data || !data->handle) {
                    info.GetReturnValue().Set(v8_convert::ToV8(isolate, 0));
                    return;
                }

                // Get file length using file descriptor
                int32_t length = _filelength(_fileno(data->handle));
                info.GetReturnValue().Set(v8_convert::ToV8(isolate, length));
            });
        /// @description File path as supplied to File.open, relative to the scripts folder.
        /// @type {string}
        Property(
            isolate, inst, "path", +[](v8::Local<v8::Name> property, const v8::PropertyCallbackInfo<v8::Value>& info) {
                auto* isolate = info.GetIsolate();
                auto self = info.Holder();
                auto* data = Unwrap(self);

                if (!data) {
                    info.GetReturnValue().SetEmptyString();
                    return;
                }

                info.GetReturnValue().Set(v8_convert::ToV8(isolate, data->path));
            });
        /// @description Current read/write position in the file, as a byte offset from the start.
        /// @type {number}
        Property(
            isolate, inst, "position",
            +[](v8::Local<v8::Name> property, const v8::PropertyCallbackInfo<v8::Value>& info) {
                auto* isolate = info.GetIsolate();
                auto self = info.Holder();
                auto* data = Unwrap(self);

                if (!data || !data->handle) {
                    info.GetReturnValue().Set(v8_convert::ToV8(isolate, 0));
                    return;
                }

                int32_t pos = static_cast<int32_t>(ftell(data->handle));
                info.GetReturnValue().Set(v8_convert::ToV8(isolate, pos));
            });
        /// @description True when the end-of-file indicator is set on the stream.
        /// @type {boolean}
        Property(
            isolate, inst, "eof", +[](v8::Local<v8::Name> property, const v8::PropertyCallbackInfo<v8::Value>& info) {
                auto* isolate = info.GetIsolate();
                auto self = info.Holder();
                auto* data = Unwrap(self);

                if (!data || !data->handle) {
                    info.GetReturnValue().Set(v8_convert::ToV8(isolate, true));
                    return;
                }

                info.GetReturnValue().Set(v8_convert::ToV8(isolate, feof(data->handle) != 0));
            });
        /// @description Last access time of the file, as a Unix timestamp in seconds since the epoch.
        /// @type {number}
        Property(
            isolate, inst, "accessed",
            +[](v8::Local<v8::Name> property, const v8::PropertyCallbackInfo<v8::Value>& info) {
                auto* isolate = info.GetIsolate();
                auto self = info.Holder();
                auto* data = Unwrap(self);

                if (!data || !data->handle) {
                    info.GetReturnValue().Set(v8_convert::ToV8(isolate, 0.0));
                    return;
                }

                struct _stat fileStat = {};
                _fstat(_fileno(data->handle), &fileStat);
                info.GetReturnValue().Set(v8_convert::ToV8(isolate, static_cast<double>(fileStat.st_atime)));
            });
        /// @description Creation time of the file, as a Unix timestamp in seconds since the epoch.
        /// @type {number}
        Property(
            isolate, inst, "created",
            +[](v8::Local<v8::Name> property, const v8::PropertyCallbackInfo<v8::Value>& info) {
                auto* isolate = info.GetIsolate();
                auto self = info.Holder();
                auto* data = Unwrap(self);

                if (!data || !data->handle) {
                    info.GetReturnValue().Set(v8_convert::ToV8(isolate, 0.0));
                    return;
                }

                struct _stat fileStat = {};
                _fstat(_fileno(data->handle), &fileStat);
                info.GetReturnValue().Set(v8_convert::ToV8(isolate, static_cast<double>(fileStat.st_ctime)));
            });
        /// @description Last modification time of the file, as a Unix timestamp in seconds since the epoch.
        /// @type {number}
        Property(
            isolate, inst, "modified",
            +[](v8::Local<v8::Name> property, const v8::PropertyCallbackInfo<v8::Value>& info) {
                auto* isolate = info.GetIsolate();
                auto self = info.Holder();
                auto* data = Unwrap(self);

                if (!data || !data->handle) {
                    info.GetReturnValue().Set(v8_convert::ToV8(isolate, 0.0));
                    return;
                }

                struct _stat fileStat = {};
                _fstat(_fileno(data->handle), &fileStat);
                info.GetReturnValue().Set(v8_convert::ToV8(isolate, static_cast<double>(fileStat.st_mtime)));
            });
        /// @description Whether the stream is flushed to disk automatically after every write() call. Assigned values
        /// are coerced to boolean via JS truthiness.
        /// @type {boolean}
        Property(
            isolate, inst, "autoflush",
            +[](v8::Local<v8::Name> property, const v8::PropertyCallbackInfo<v8::Value>& info) {
                auto* isolate = info.GetIsolate();
                auto self = info.Holder();
                auto* data = Unwrap(self);

                if (!data) {
                    info.GetReturnValue().Set(v8_convert::ToV8(isolate, false));
                    return;
                }

                info.GetReturnValue().Set(v8_convert::ToV8(isolate, data->autoflush));
            },
            +[](v8::Local<v8::Name> property, v8::Local<v8::Value> value, const v8::PropertyCallbackInfo<void>& info) {
                auto* isolate = info.GetIsolate();
                auto self = info.Holder();
                auto* data = Unwrap(self);

                if (data) {
                    data->autoflush = value->BooleanValue(isolate);
                }
            });

        /// @description Closes the file, unlocking it first if it was opened locked, so it can later be reopen()ed.
        /// @signature close()
        /// @returns {File} - This file object, for chaining.
        /// @throws {Error} - If the file is not open.
        /// @throws {Error} - If closing the underlying file fails.
        Method(
            isolate, proto, "close", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
                auto* isolate = args.GetIsolate();
                auto self = args.This();
                auto* data = Unwrap(self);

                if (!data) {
                    v8_error::ThrowError(isolate, "Couldn't get file object");
                    return;
                }

                if (!data->handle) {
                    v8_error::ThrowError(isolate, "File is not open");
                    return;
                }

                // Unlock before closing if locked
                if (data->isLocked) {
                    _unlock_file(data->handle);
                }
                if (fclose(data->handle) != 0) {
                    data->handle = nullptr;
                    v8_error::ThrowError(isolate, "Close failed");
                    return;
                }
                data->handle = nullptr;

                // Return this for chaining
                args.GetReturnValue().Set(self);
            });
        /// @description Reopens a previously closed file using the same path and mode, restoring its lock state.
        /// @signature reopen()
        /// @returns {File} - This file object, for chaining.
        /// @throws {Error} - If the file is not closed.
        /// @throws {Error} - If the file cannot be opened.
        Method(
            isolate, proto, "reopen", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
                auto* isolate = args.GetIsolate();
                auto self = args.This();
                auto* data = Unwrap(self);

                if (!data) {
                    v8_error::ThrowError(isolate, "Couldn't get file object");
                    return;
                }

                if (data->handle) {
                    v8_error::ThrowError(isolate, "File is not closed");
                    return;
                }

                // Reopen file with same mode
                std::string pathStr = data->path.string();
                data->handle = file_detail::FileOpenRelScript(isolate, pathStr, MODE_STRINGS[data->mode]);
                if (!data->handle) {
                    return;  // FileOpenRelScript already threw
                }

                if (data->isLocked) {
                    _lock_file(data->handle);
                }

                args.GetReturnValue().Set(self);
            });
        /// @description Reads up to count units from the file, advancing the position. A leading BOM is skipped when
        /// reading from offset 0 in text mode.
        /// @signature read(count: number)
        /// @param count {number} - Text mode: number of bytes to read; must be > 0.
        /// @returns {string} - The bytes read, as a string. undefined if the file is not open.
        /// @signature read(count: number)
        /// @param count {number} - Binary mode: number of 32-bit integers to read; must be > 0.
        /// @returns {number|Array<number>} - A single number when count is 1, otherwise an Array of numbers. undefined
        /// if the file is not open.
        /// @throws {Error} - If count is not greater than 0.
        /// @throws {Error} - If reading from the file fails.
        Method(
            isolate, proto, "read", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
                auto* isolate = args.GetIsolate();
                auto context = isolate->GetCurrentContext();
                auto self = args.This();
                auto* data = Unwrap(self);

                if (!data || !data->handle) {
                    return;  // Return undefined
                }

                if (args.Length() < 1) {
                    v8_error::ThrowError(isolate, "Invalid arguments");
                    return;
                }
                int32_t count = v8_convert::ToInt32(isolate, args[0]);
                if (count <= 0) {
                    v8_error::ThrowError(isolate, "Invalid arguments");
                    return;
                }

                clearerr(data->handle);

                if (data->mode > 2) {
                    // Binary mode: read count ints
                    std::vector<int32_t> result(count, 0);
                    size_t readCount = fread(result.data(), sizeof(int32_t), count, data->handle);

                    if (readCount != static_cast<size_t>(count) && ferror(data->handle)) {
                        v8_error::ThrowError(isolate, "Read failed");
                        return;
                    }

                    if (count == 1) {
                        args.GetReturnValue().Set(v8_convert::ToV8(isolate, result[0]));
                    } else {
                        auto arr = v8::Array::New(isolate, count);
                        for (int32_t i = 0; i < count; i++) {
                            arr->Set(context, i, v8_convert::ToV8(isolate, result[i])).Check();
                        }
                        args.GetReturnValue().Set(arr);
                    }
                } else {
                    // Text mode
                    bool isBegin = (ftell(data->handle) == 0);

                    fflush(data->handle);

                    std::vector<char> result(count + 1, 0);
                    size_t readCount = fread(result.data(), sizeof(char), count, data->handle);

                    if (readCount != static_cast<size_t>(count) && ferror(data->handle)) {
                        v8_error::ThrowError(isolate, "Read failed");
                        return;
                    }

                    size_t offset = 0;
                    if (isBegin) {
                        offset = file_detail::SkipBom(result.data(), readCount);
                    }

                    args.GetReturnValue().Set(
                        v8_convert::ToV8(isolate, std::string(result.data() + offset, readCount - offset)));
                }
            });
        /// @description Reads a single line from the file, newline excluded, advancing the position. A leading BOM is
        /// skipped when reading from offset 0.
        /// @signature readLine()
        /// @returns {string} - The next line of text. undefined if the file is not open.
        /// @throws {Error} - If the position is already at end-of-file.
        Method(
            isolate, proto, "readLine", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
                auto* isolate = args.GetIsolate();
                auto self = args.This();
                auto* data = Unwrap(self);

                if (!data || !data->handle) {
                    return;  // Return undefined
                }

                // Reference returns NULL from readLine when at EOF, then throws "Read failed"
                if (feof(data->handle)) {
                    v8_error::ThrowError(isolate, "Read failed");
                    return;
                }

                bool isBegin = (ftell(data->handle) == 0);

                std::string line = file_detail::ReadLine(data->handle);

                if (isBegin) {
                    size_t offset = file_detail::SkipBom(line.data(), line.size());
                    if (offset > 0) {
                        line = line.substr(offset);
                    }
                }

                args.GetReturnValue().Set(v8_convert::ToV8(isolate, line));
            });
        /// @description Reads all remaining lines from the current position to end-of-file, newlines excluded. A
        /// leading BOM is skipped on the first line when reading from offset 0.
        /// @signature readAllLines()
        /// @returns {Array<string>} - Array of the remaining lines. undefined if the file is not open.
        /// @throws {Error} - If reading from the file fails.
        Method(
            isolate, proto, "readAllLines", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
                auto* isolate = args.GetIsolate();
                auto context = isolate->GetCurrentContext();
                auto self = args.This();
                auto* data = Unwrap(self);

                if (!data || !data->handle) {
                    return;  // Return undefined
                }

                auto arr = v8::Array::New(isolate, 0);
                int32_t idx = 0;

                while (true) {
                    if (ferror(data->handle)) {
                        v8_error::ThrowError(isolate, "Read failed");
                        return;
                    }
                    if (feof(data->handle))
                        break;

                    bool isBegin = (ftell(data->handle) == 0);

                    std::string line = file_detail::ReadLine(data->handle);

                    if (ferror(data->handle)) {
                        v8_error::ThrowError(isolate, "Read failed");
                        return;
                    }
                    // EOF with no data means we hit EOF without reading new content - don't append
                    if (feof(data->handle) && line.empty())
                        break;

                    if (isBegin) {
                        size_t offset = file_detail::SkipBom(line.data(), line.size());
                        if (offset > 0) {
                            line = line.substr(offset);
                        }
                    }

                    arr->Set(context, idx++, v8_convert::ToV8(isolate, line)).Check();
                }

                args.GetReturnValue().Set(arr);
            });
        /// @description Reads the entire file contents from the start as a string. A leading BOM is skipped when the
        /// prior position was offset 0.
        /// @signature readAll()
        /// @returns {string} - The full file contents, or "" if empty. undefined if the file is not open.
        /// @throws {Error} - If reading from the file fails.
        Method(
            isolate, proto, "readAll", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
                auto* isolate = args.GetIsolate();
                auto self = args.This();
                auto* data = Unwrap(self);

                if (!data || !data->handle) {
                    return;  // Return undefined
                }

                bool isBegin = (ftell(data->handle) == 0);

                // Seek to end to get file size
                fseek(data->handle, 0, SEEK_END);
                int32_t size = static_cast<int32_t>(ftell(data->handle));
                fseek(data->handle, 0, SEEK_SET);

                if (size <= 0) {
                    args.GetReturnValue().Set(v8_convert::ToV8(isolate, ""));
                    return;
                }

                std::vector<char> contents(size + 1, 0);
                size_t readCount = fread(contents.data(), sizeof(char), size, data->handle);

                if (readCount != static_cast<size_t>(size) && ferror(data->handle)) {
                    v8_error::ThrowError(isolate, "Read failed");
                    return;
                }

                size_t offset = 0;
                if (isBegin) {
                    offset = file_detail::SkipBom(contents.data(), readCount);
                }

                args.GetReturnValue().Set(
                    v8_convert::ToV8(isolate, std::string(contents.data() + offset, readCount - offset)));
            });
        /// @description Writes the given values to the file in order, then flushes if autoflush is enabled. Text mode
        /// writes each value's text form; binary mode serializes per type (integer as 4-byte int32, non-integer number
        /// as 8-byte double, boolean as one byte, null/undefined as 4 zero bytes, string as raw bytes).
        /// @signature write(...values: any)
        /// @param values {any} - Zero or more values to write; each is serialized per the file's text/binary mode.
        /// @returns {File} - This file object, for chaining.
        Method(
            isolate, proto, "write", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
                auto* isolate = args.GetIsolate();
                auto self = args.This();
                auto* data = Unwrap(self);

                // Reference silently returns this when file is not open
                if (data && data->handle) {
                    bool isBinary = data->mode > 2;

                    // Process each argument and write to file
                    for (int32_t i = 0; i < args.Length(); i++) {
                        file_detail::WriteValue(data->handle, isolate, args[i], isBinary);
                    }

                    if (data->autoflush) {
                        fflush(data->handle);
                    }
                }

                args.GetReturnValue().Set(self);
            });
        /// @description Moves the file position by offset, relative to the current position unless fromStart rewinds to
        /// the beginning first.
        /// @signature seek(offset: number)
        /// @param offset {number} - Number of bytes to advance from the current position.
        /// @returns {File} - This file object, for chaining.
        /// @signature seek(offset: number, isLines: boolean)
        /// @param offset {number} - Number of bytes (isLines false) or lines (isLines true) to advance.
        /// @param isLines {boolean} - If true, advance by lines instead of bytes.
        /// @returns {File} - This file object, for chaining.
        /// @signature seek(offset: number, isLines: boolean, fromStart: boolean)
        /// @param offset {number} - Number of bytes (isLines false) or lines (isLines true) to advance.
        /// @param isLines {boolean} - If true, advance by lines instead of bytes.
        /// @param fromStart {boolean} - If true, rewind to the start and clear errors before applying offset.
        /// @returns {File} - This file object, for chaining.
        /// @throws {Error} - If the file is not open.
        /// @throws {Error} - If the seek fails.
        Method(
            isolate, proto, "seek", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
                auto* isolate = args.GetIsolate();
                auto self = args.This();
                auto* data = Unwrap(self);

                if (!data || !data->handle) {
                    v8_error::ThrowError(isolate, "File is not open");
                    return;
                }

                if (args.Length() < 1) {
                    v8_error::ThrowError(isolate, "Not enough parameters");
                    return;
                }

                int32_t offset = v8_convert::ToInt32(isolate, args[0]);
                bool isLines = args.Length() > 1 ? v8_convert::ToBool(isolate, args[1]) : false;
                bool fromStart = args.Length() > 2 ? v8_convert::ToBool(isolate, args[2]) : false;

                if (fromStart) {
                    fseek(data->handle, 0, SEEK_SET);
                    clearerr(data->handle);  // rewind() equivalent: seek + clear error
                }

                if (!isLines) {
                    if (fseek(data->handle, offset, SEEK_CUR) != 0) {
                        v8_error::ThrowError(isolate, "Seek failed");
                        return;
                    }
                } else {
                    // Seek by lines: read and discard 'offset' lines
                    for (int32_t i = 0; i < offset; i++) {
                        file_detail::ReadLine(data->handle);
                    }
                }

                args.GetReturnValue().Set(self);
            });
        /// @description Flushes any buffered writes to disk.
        /// @signature flush()
        /// @returns {File} - This file object, for chaining.
        Method(
            isolate, proto, "flush", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
                auto self = args.This();
                auto* data = Unwrap(self);

                if (data && data->handle) {
                    fflush(data->handle);
                }

                args.GetReturnValue().Set(self);
            });
        /// @description Seeks the file position back to the beginning.
        /// @signature reset()
        /// @returns {File} - This file object, for chaining.
        /// @throws {Error} - If the seek fails.
        Method(
            isolate, proto, "reset", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
                auto* isolate = args.GetIsolate();
                auto self = args.This();
                auto* data = Unwrap(self);

                if (data && data->handle) {
                    if (fseek(data->handle, 0L, SEEK_SET) != 0) {
                        v8_error::ThrowError(isolate, "Seek failed");
                        return;
                    }
                }

                args.GetReturnValue().Set(self);
            });
        /// @description Seeks the file position to the end of the file.
        /// @signature end()
        /// @returns {File} - This file object, for chaining.
        /// @throws {Error} - If the seek fails.
        Method(
            isolate, proto, "end", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
                auto* isolate = args.GetIsolate();
                auto self = args.This();
                auto* data = Unwrap(self);

                if (data && data->handle) {
                    if (fseek(data->handle, 0L, SEEK_END) != 0) {
                        v8_error::ThrowError(isolate, "Seek failed");
                        return;
                    }
                }

                args.GetReturnValue().Set(self);
            });

        /// @description Opens a file relative to the scripts folder and returns a new File object. This is the only way
        /// to construct a File.
        /// @signature open(path: string, mode: number)
        /// @param path {string} - File path relative to the scripts folder; must be non-empty.
        /// @param mode {number} - Open mode: 0 = read (FILE_READ), 1 = write (FILE_WRITE), 2 = append (FILE_APPEND).
        /// @returns {File} - A new File object. undefined if instance creation fails.
        /// @signature open(path: string, mode: number, binaryMode?: boolean, autoflush?: boolean, lockFile?: boolean)
        /// @param path {string} - File path relative to the scripts folder; must be non-empty.
        /// @param mode {number} - Open mode: 0 = read (FILE_READ), 1 = write (FILE_WRITE), 2 = append (FILE_APPEND).
        /// @param binaryMode {boolean} - Optional, default false. If true, open in binary mode where read/write operate
        /// on 32-bit integers.
        /// @param autoflush {boolean} - Optional, default false. If true, flush to disk after each write.
        /// @param lockFile {boolean} - Optional, default false. If true, take an exclusive lock on the file.
        /// @returns {File} - A new File object. undefined if instance creation fails.
        /// @throws {Error} - If path is empty.
        /// @throws {Error} - If mode is not one of FILE_READ, FILE_WRITE, or FILE_APPEND.
        /// @throws {Error} - If the file cannot be opened.
        StaticMethod(
            isolate, tpl, "open", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
                // File.open(path, mode, [binaryMode], [autoflush], [lockFile])
                auto* isolate = args.GetIsolate();
                auto context = isolate->GetCurrentContext();

                if (args.Length() < 2) {
                    v8_error::ThrowError(isolate, "Not enough parameters, 2 or more expected");
                    return;
                }

                if (!args[0]->IsString()) {
                    v8_error::ThrowError(isolate, "Parameter 1 must be a string (path)");
                    return;
                }

                if (!args[1]->IsNumber()) {
                    v8_error::ThrowError(isolate, "Parameter 2 must be a number (mode)");
                    return;
                }

                std::string path = v8_convert::ToString(isolate, args[0]);
                int32_t mode = v8_convert::ToInt32(isolate, args[1]);
                bool binary = args.Length() > 2 ? v8_convert::ToBool(isolate, args[2]) : false;
                bool autoflush = args.Length() > 3 ? v8_convert::ToBool(isolate, args[3]) : false;
                bool lockFile = args.Length() > 4 ? v8_convert::ToBool(isolate, args[4]) : false;

                // Validate path
                if (path.empty()) {
                    v8_error::ThrowError(isolate, "Invalid file name");
                    return;
                }

                // Validate mode
                if (mode < static_cast<int32_t>(FileMode::Read) || mode > static_cast<int32_t>(FileMode::Append)) {
                    v8_error::ThrowError(isolate, "Invalid file mode");
                    return;
                }

                // Adjust mode for binary
                if (binary) {
                    mode += 3;
                }

                // Open the file
                FILE* fp = file_detail::FileOpenRelScript(isolate, path, MODE_STRINGS[mode]);
                if (!fp) {
                    return;  // FileOpenRelScript already threw
                }

                // Lock the file if requested
                if (lockFile) {
                    _lock_file(fp);
                }

                // Create and attach FileData
                auto data = std::make_unique<FileData>();
                data->mode = mode;
                data->path = path;
                data->autoflush = autoflush;
                data->isLocked = lockFile;
                data->handle = fp;

                // Create File object via CreateInstance (bypasses NOT_CONSTRUCTABLE guard)
                auto obj = CreateInstance(isolate, context, std::move(data));
                if (obj.IsEmpty())
                    return;

                args.GetReturnValue().Set(obj);
            });
    }
};

}  // namespace d2bs::api::classes
