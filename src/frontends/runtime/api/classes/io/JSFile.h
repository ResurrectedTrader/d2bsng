#pragma once

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include <io.h>
#include <spdlog/spdlog.h>
#include <sys/stat.h>

#include "api/core/Class.h"
#include "api/core/Convert.h"
#include "api/core/Error.h"
#include "api/globals/Constants.h"
#include "utils/utils.h"

namespace d2bs::api::classes {

using globals::FileMode;

namespace file_detail {
inline spdlog::logger& Log() {
    static const auto LOGGER = utils::GetLogger("api.file");
    return *LOGGER;
}

FILE* FileOpenRelScript(ub::Isolate& isolate, const std::string& relativePath, const wchar_t* mode);
std::string ReadLine(FILE* fptr);
bool WriteValue(FILE* fptr, const ub::Context& context, const ub::Local<ub::Value>& value, bool isBinary);
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
                file_detail::Log().warn("Close failed for file: {}", path.string());
            }
        }
    }
};

// JSFile - script wrapper for file operations. Instances come from File.open(); the class is not
// constructable.
class JSFile : public ClassBase<JSFile, FileData> {
   public:
    static constexpr std::string_view ClassName = "File";

    // Mode strings indexed by mode value (0-5)
    // NOLINTBEGIN(cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays)
    static constexpr const wchar_t* MODE_STRINGS[] = {L"rt", L"w+t", L"a+t", L"rb", L"w+b", L"a+b"};
    // NOLINTEND(cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays)

    static void Configure(const ub::Class<FileData>& cls) {
        /// @description True while the file is open, not at end-of-file, and has no pending error.
        /// @type {boolean}
        Property(
            cls, "readable", +[](const ub::Local<ub::Name>& /*property*/, const ub::PropertyCallbackInfo& info) {
                auto* data = Unwrap(info.This());
                bool readable = data && data->handle && !feof(data->handle) && !ferror(data->handle);
                info.GetReturnValue().Set(readable);
            });
        /// @description True while the file is open in write or append mode and has no pending error.
        /// @type {boolean}
        Property(
            cls, "writeable", +[](const ub::Local<ub::Name>& /*property*/, const ub::PropertyCallbackInfo& info) {
                auto* data = Unwrap(info.This());
                bool writeable = data && data->handle && !ferror(data->handle) &&
                                 (data->mode % 3) > static_cast<int32_t>(FileMode::Read);
                info.GetReturnValue().Set(writeable);
            });
        /// @description True while the file is open and has no pending error.
        /// @type {boolean}
        Property(
            cls, "seekable", +[](const ub::Local<ub::Name>& /*property*/, const ub::PropertyCallbackInfo& info) {
                auto* data = Unwrap(info.This());
                info.GetReturnValue().Set(data && data->handle && !ferror(data->handle));
            });
        /// @description Base open mode without the binary flag: 0 = read (FILE_READ), 1 = write (FILE_WRITE), 2 =
        /// append (FILE_APPEND).
        /// @type {number}
        Property(
            cls, "mode", +[](const ub::Local<ub::Name>& /*property*/, const ub::PropertyCallbackInfo& info) {
                auto* data = Unwrap(info.This());
                if (!data) {
                    info.GetReturnValue().Set(0);
                    return;
                }

                // Base mode (0, 1, or 2) without the binary flag
                info.GetReturnValue().Set(data->mode % 3);
            });
        /// @description True if the file was opened in binary mode, where read/write operate on 32-bit integers rather
        /// than text.
        /// @type {boolean}
        Property(
            cls, "binaryMode", +[](const ub::Local<ub::Name>& /*property*/, const ub::PropertyCallbackInfo& info) {
                auto* data = Unwrap(info.This());
                // 3, 4 and 5 are the binary versions of read, write and append
                info.GetReturnValue().Set(data && data->mode > 2);
            });
        /// @description Total length of the file in bytes.
        /// @type {number}
        Property(
            cls, "length", +[](const ub::Local<ub::Name>& /*property*/, const ub::PropertyCallbackInfo& info) {
                auto* data = Unwrap(info.This());
                if (!data || !data->handle) {
                    info.GetReturnValue().Set(0);
                    return;
                }

                int32_t length = _filelength(_fileno(data->handle));
                info.GetReturnValue().Set(length);
            });
        /// @description File path as supplied to File.open, relative to the scripts folder.
        /// @type {string}
        Property(
            cls, "path", +[](const ub::Local<ub::Name>& /*property*/, const ub::PropertyCallbackInfo& info) {
                auto* data = Unwrap(info.This());
                if (!data) {
                    info.GetReturnValue().Set(convert::ToJS(info.GetIsolate(), ""));
                    return;
                }

                info.GetReturnValue().Set(convert::ToJS(info.GetIsolate(), data->path));
            });
        /// @description Current read/write position in the file, as a byte offset from the start.
        /// @type {number}
        Property(
            cls, "position", +[](const ub::Local<ub::Name>& /*property*/, const ub::PropertyCallbackInfo& info) {
                auto* data = Unwrap(info.This());
                if (!data || !data->handle) {
                    info.GetReturnValue().Set(0);
                    return;
                }

                info.GetReturnValue().Set(static_cast<int32_t>(ftell(data->handle)));
            });
        /// @description True when the end-of-file indicator is set on the stream.
        /// @type {boolean}
        Property(
            cls, "eof", +[](const ub::Local<ub::Name>& /*property*/, const ub::PropertyCallbackInfo& info) {
                auto* data = Unwrap(info.This());
                if (!data || !data->handle) {
                    info.GetReturnValue().Set(true);
                    return;
                }

                info.GetReturnValue().Set(feof(data->handle) != 0);
            });
        /// @description Last access time of the file, as a Unix timestamp in seconds since the epoch.
        /// @type {number}
        Property(
            cls, "accessed", +[](const ub::Local<ub::Name>& /*property*/, const ub::PropertyCallbackInfo& info) {
                auto* data = Unwrap(info.This());
                if (!data || !data->handle) {
                    info.GetReturnValue().Set(0.0);
                    return;
                }

                struct _stat fileStat = {};
                _fstat(_fileno(data->handle), &fileStat);
                info.GetReturnValue().Set(static_cast<double>(fileStat.st_atime));
            });
        /// @description Creation time of the file, as a Unix timestamp in seconds since the epoch.
        /// @type {number}
        Property(
            cls, "created", +[](const ub::Local<ub::Name>& /*property*/, const ub::PropertyCallbackInfo& info) {
                auto* data = Unwrap(info.This());
                if (!data || !data->handle) {
                    info.GetReturnValue().Set(0.0);
                    return;
                }

                struct _stat fileStat = {};
                _fstat(_fileno(data->handle), &fileStat);
                info.GetReturnValue().Set(static_cast<double>(fileStat.st_ctime));
            });
        /// @description Last modification time of the file, as a Unix timestamp in seconds since the epoch.
        /// @type {number}
        Property(
            cls, "modified", +[](const ub::Local<ub::Name>& /*property*/, const ub::PropertyCallbackInfo& info) {
                auto* data = Unwrap(info.This());
                if (!data || !data->handle) {
                    info.GetReturnValue().Set(0.0);
                    return;
                }

                struct _stat fileStat = {};
                _fstat(_fileno(data->handle), &fileStat);
                info.GetReturnValue().Set(static_cast<double>(fileStat.st_mtime));
            });
        /// @description Whether the stream is flushed to disk automatically after every write() call. Assigned values
        /// are coerced to boolean via JS truthiness.
        /// @type {boolean}
        Property(
            cls, "autoflush",
            +[](const ub::Local<ub::Name>& /*property*/, const ub::PropertyCallbackInfo& info) {
                auto* data = Unwrap(info.This());
                info.GetReturnValue().Set(data && data->autoflush);
            },
            +[](const ub::Local<ub::Name>& /*property*/, const ub::Local<ub::Value>& value,
                const ub::PropertyCallbackInfo& info) {
                auto* data = Unwrap(info.This());
                if (data) {
                    data->autoflush = convert::ToBool(info.GetContext(), value);
                }
            });

        /// @description Closes the file, unlocking it first if it was opened locked, so it can later be reopen()ed.
        /// @signature close()
        /// @returns {File} - This file object, for chaining.
        /// @throws {Error} - If the file is not open.
        /// @throws {Error} - If closing the underlying file fails.
        Method(
            cls, "close", +[](const ub::CallbackInfo& args) {
                auto& isolate = args.GetIsolate();
                auto* data = Unwrap(args.This());

                if (!data) {
                    error::ThrowError(isolate, "Couldn't get file object");
                    return;
                }

                if (!data->handle) {
                    error::ThrowError(isolate, "File is not open");
                    return;
                }

                if (data->isLocked) {
                    _unlock_file(data->handle);
                }
                if (fclose(data->handle) != 0) {
                    data->handle = nullptr;
                    error::ThrowError(isolate, "Close failed");
                    return;
                }
                data->handle = nullptr;

                args.GetReturnValue().Set(args.This());
            });
        /// @description Reopens a previously closed file using the same path and mode, restoring its lock state.
        /// @signature reopen()
        /// @returns {File} - This file object, for chaining.
        /// @throws {Error} - If the file is not closed.
        /// @throws {Error} - If the file cannot be opened.
        Method(
            cls, "reopen", +[](const ub::CallbackInfo& args) {
                auto& isolate = args.GetIsolate();
                auto* data = Unwrap(args.This());

                if (!data) {
                    error::ThrowError(isolate, "Couldn't get file object");
                    return;
                }

                if (data->handle) {
                    error::ThrowError(isolate, "File is not closed");
                    return;
                }

                std::string pathStr = data->path.string();
                data->handle = file_detail::FileOpenRelScript(isolate, pathStr, MODE_STRINGS[data->mode]);
                if (!data->handle) {
                    return;  // FileOpenRelScript already threw
                }

                if (data->isLocked) {
                    _lock_file(data->handle);
                }

                args.GetReturnValue().Set(args.This());
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
            cls, "read", +[](const ub::CallbackInfo& args) {
                auto& isolate = args.GetIsolate();
                const auto& context = args.GetContext();
                auto* data = Unwrap(args.This());

                if (!data || !data->handle) {
                    return;  // Return undefined
                }

                if (args.Length() < 1) {
                    error::ThrowError(isolate, "Invalid arguments");
                    return;
                }
                int32_t count = convert::ToInt32(context, args[0]);
                if (count <= 0) {
                    error::ThrowError(isolate, "Invalid arguments");
                    return;
                }

                clearerr(data->handle);

                if (data->mode > 2) {
                    // Binary mode: read count ints
                    std::vector result(count, 0);
                    size_t readCount = fread(result.data(), sizeof(int32_t), count, data->handle);

                    if (readCount != static_cast<size_t>(count) && ferror(data->handle)) {
                        error::ThrowError(isolate, "Read failed");
                        return;
                    }

                    if (count == 1) {
                        args.GetReturnValue().Set(result[0]);
                    } else {
                        auto arr = ub::Array::New(context, static_cast<uint32_t>(count));
                        if (!arr) {
                            return;
                        }
                        for (uint32_t i = 0; i < static_cast<uint32_t>(count); i++) {
                            if (!arr->Set(context, i, convert::ToJS(isolate, result[i]))) {
                                return;
                            }
                        }
                        args.GetReturnValue().Set(*arr);
                    }
                } else {
                    // Text mode
                    bool isBegin = (ftell(data->handle) == 0);

                    fflush(data->handle);

                    std::vector<char> result(count + 1, 0);
                    size_t readCount = fread(result.data(), sizeof(char), count, data->handle);

                    if (readCount != static_cast<size_t>(count) && ferror(data->handle)) {
                        error::ThrowError(isolate, "Read failed");
                        return;
                    }

                    size_t offset = 0;
                    if (isBegin) {
                        offset = file_detail::SkipBom(result.data(), readCount);
                    }

                    if (auto text = ub::String::NewFromUtf8(
                            isolate, std::string_view(result.data() + offset, readCount - offset))) {
                        args.GetReturnValue().Set(*text);
                    }
                }
            });
        /// @description Reads a single line from the file, newline excluded, advancing the position. A leading BOM is
        /// skipped when reading from offset 0.
        /// @signature readLine()
        /// @returns {string} - The next line of text. undefined if the file is not open.
        /// @throws {Error} - If the position is already at end-of-file.
        Method(
            cls, "readLine", +[](const ub::CallbackInfo& args) {
                auto& isolate = args.GetIsolate();
                auto* data = Unwrap(args.This());

                if (!data || !data->handle) {
                    return;  // Return undefined
                }

                // Reference returns NULL from readLine when at EOF, then throws "Read failed"
                if (feof(data->handle)) {
                    error::ThrowError(isolate, "Read failed");
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

                if (auto text = ub::String::NewFromUtf8(isolate, line)) {
                    args.GetReturnValue().Set(*text);
                }
            });
        /// @description Reads all remaining lines from the current position to end-of-file, newlines excluded. A
        /// leading BOM is skipped on the first line when reading from offset 0.
        /// @signature readAllLines()
        /// @returns {Array<string>} - Array of the remaining lines. undefined if the file is not open.
        /// @throws {Error} - If reading from the file fails.
        Method(
            cls, "readAllLines", +[](const ub::CallbackInfo& args) {
                auto& isolate = args.GetIsolate();
                const auto& context = args.GetContext();
                auto* data = Unwrap(args.This());

                if (!data || !data->handle) {
                    return;  // Return undefined
                }

                auto arr = ub::Array::New(context, 0);
                if (!arr) {
                    return;
                }
                uint32_t idx = 0;

                while (true) {
                    if (ferror(data->handle)) {
                        error::ThrowError(isolate, "Read failed");
                        return;
                    }
                    if (feof(data->handle)) {
                        break;
                    }

                    bool isBegin = (ftell(data->handle) == 0);

                    std::string line = file_detail::ReadLine(data->handle);

                    if (ferror(data->handle)) {
                        error::ThrowError(isolate, "Read failed");
                        return;
                    }
                    // EOF with no data means we hit EOF without reading new content - don't append
                    if (feof(data->handle) && line.empty()) {
                        break;
                    }

                    if (isBegin) {
                        size_t offset = file_detail::SkipBom(line.data(), line.size());
                        if (offset > 0) {
                            line = line.substr(offset);
                        }
                    }

                    auto text = ub::String::NewFromUtf8(isolate, line);
                    if (!text || !arr->Set(context, idx++, *text)) {
                        return;
                    }
                }

                args.GetReturnValue().Set(*arr);
            });
        /// @description Reads the entire file contents from the start as a string. A leading BOM is skipped when the
        /// prior position was offset 0.
        /// @signature readAll()
        /// @returns {string} - The full file contents, or "" if empty. undefined if the file is not open.
        /// @throws {Error} - If reading from the file fails.
        Method(
            cls, "readAll", +[](const ub::CallbackInfo& args) {
                auto& isolate = args.GetIsolate();
                auto* data = Unwrap(args.This());

                if (!data || !data->handle) {
                    return;  // Return undefined
                }

                bool isBegin = (ftell(data->handle) == 0);

                fseek(data->handle, 0, SEEK_END);
                int32_t size = static_cast<int32_t>(ftell(data->handle));
                fseek(data->handle, 0, SEEK_SET);

                if (size <= 0) {
                    args.GetReturnValue().Set(convert::ToJS(isolate, ""));
                    return;
                }

                std::vector<char> contents(size + 1, 0);
                size_t readCount = fread(contents.data(), sizeof(char), size, data->handle);

                if (readCount != static_cast<size_t>(size) && ferror(data->handle)) {
                    error::ThrowError(isolate, "Read failed");
                    return;
                }

                size_t offset = 0;
                if (isBegin) {
                    offset = file_detail::SkipBom(contents.data(), readCount);
                }

                if (auto text = ub::String::NewFromUtf8(
                        isolate, std::string_view(contents.data() + offset, readCount - offset))) {
                    args.GetReturnValue().Set(*text);
                }
            });
        /// @description Writes the given values to the file in order, then flushes if autoflush is enabled. Text mode
        /// writes each value's text form; binary mode serializes per type (integer as 4-byte int32, non-integer number
        /// as 8-byte double, boolean as one byte, null/undefined as 4 zero bytes, string as raw bytes).
        /// @signature write(...values: any)
        /// @param values {any} - Zero or more values to write; each is serialized per the file's text/binary mode.
        /// @returns {File} - This file object, for chaining.
        Method(
            cls, "write", +[](const ub::CallbackInfo& args) {
                auto* data = Unwrap(args.This());

                // Reference silently returns this when file is not open
                if (data && data->handle) {
                    bool isBinary = data->mode > 2;

                    for (uint32_t i = 0; i < args.Length(); i++) {
                        file_detail::WriteValue(data->handle, args.GetContext(), args[i], isBinary);
                    }

                    if (data->autoflush) {
                        fflush(data->handle);
                    }
                }

                args.GetReturnValue().Set(args.This());
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
            cls, "seek", +[](const ub::CallbackInfo& args) {
                auto& isolate = args.GetIsolate();
                const auto& context = args.GetContext();
                auto* data = Unwrap(args.This());

                if (!data || !data->handle) {
                    error::ThrowError(isolate, "File is not open");
                    return;
                }

                if (args.Length() < 1) {
                    error::ThrowError(isolate, "Not enough parameters");
                    return;
                }

                int32_t offset = convert::ToInt32(context, args[0]);
                bool isLines = args.Length() > 1 ? convert::ToBool(context, args[1]) : false;
                bool fromStart = args.Length() > 2 ? convert::ToBool(context, args[2]) : false;

                if (fromStart) {
                    fseek(data->handle, 0, SEEK_SET);
                    clearerr(data->handle);  // rewind() equivalent: seek + clear error
                }

                if (!isLines) {
                    if (fseek(data->handle, offset, SEEK_CUR) != 0) {
                        error::ThrowError(isolate, "Seek failed");
                        return;
                    }
                } else {
                    // Seek by lines: read and discard 'offset' lines
                    for (int32_t i = 0; i < offset; i++) {
                        file_detail::ReadLine(data->handle);
                    }
                }

                args.GetReturnValue().Set(args.This());
            });
        /// @description Flushes any buffered writes to disk.
        /// @signature flush()
        /// @returns {File} - This file object, for chaining.
        Method(
            cls, "flush", +[](const ub::CallbackInfo& args) {
                auto* data = Unwrap(args.This());

                if (data && data->handle) {
                    fflush(data->handle);
                }

                args.GetReturnValue().Set(args.This());
            });
        /// @description Seeks the file position back to the beginning.
        /// @signature reset()
        /// @returns {File} - This file object, for chaining.
        /// @throws {Error} - If the seek fails.
        Method(
            cls, "reset", +[](const ub::CallbackInfo& args) {
                auto* data = Unwrap(args.This());

                if (data && data->handle) {
                    if (fseek(data->handle, 0L, SEEK_SET) != 0) {
                        error::ThrowError(args.GetIsolate(), "Seek failed");
                        return;
                    }
                }

                args.GetReturnValue().Set(args.This());
            });
        /// @description Seeks the file position to the end of the file.
        /// @signature end()
        /// @returns {File} - This file object, for chaining.
        /// @throws {Error} - If the seek fails.
        Method(
            cls, "end", +[](const ub::CallbackInfo& args) {
                auto* data = Unwrap(args.This());

                if (data && data->handle) {
                    if (fseek(data->handle, 0L, SEEK_END) != 0) {
                        error::ThrowError(args.GetIsolate(), "Seek failed");
                        return;
                    }
                }

                args.GetReturnValue().Set(args.This());
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
            cls, "open", +[](const ub::CallbackInfo& args) {
                auto& isolate = args.GetIsolate();
                const auto& context = args.GetContext();

                if (args.Length() < 2) {
                    error::ThrowError(isolate, "Not enough parameters, 2 or more expected");
                    return;
                }

                if (!args[0].IsString()) {
                    error::ThrowError(isolate, "Parameter 1 must be a string (path)");
                    return;
                }

                if (!args[1].IsNumber()) {
                    error::ThrowError(isolate, "Parameter 2 must be a number (mode)");
                    return;
                }

                std::string path = convert::ToString(context, args[0]);
                int32_t mode = convert::ToInt32(context, args[1]);
                bool binary = args.Length() > 2 ? convert::ToBool(context, args[2]) : false;
                bool autoflush = args.Length() > 3 ? convert::ToBool(context, args[3]) : false;
                bool lockFile = args.Length() > 4 ? convert::ToBool(context, args[4]) : false;

                if (path.empty()) {
                    error::ThrowError(isolate, "Invalid file name");
                    return;
                }

                if (mode < static_cast<int32_t>(FileMode::Read) || mode > static_cast<int32_t>(FileMode::Append)) {
                    error::ThrowError(isolate, "Invalid file mode");
                    return;
                }

                if (binary) {
                    mode += 3;
                }

                FILE* fp = file_detail::FileOpenRelScript(isolate, path, MODE_STRINGS[mode]);
                if (!fp) {
                    return;  // FileOpenRelScript already threw
                }

                if (lockFile) {
                    _lock_file(fp);
                }

                auto data = std::make_shared<FileData>();
                data->mode = mode;
                data->path = path;
                data->autoflush = autoflush;
                data->isLocked = lockFile;
                data->handle = fp;

                auto obj = Wrap(context, std::move(data));
                if (!obj) {
                    return;
                }

                args.GetReturnValue().Set(*obj);
            });
    }
};

}  // namespace d2bs::api::classes
