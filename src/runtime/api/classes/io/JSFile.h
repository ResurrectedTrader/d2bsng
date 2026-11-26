#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <spdlog/spdlog.h>
#include <v8.h>

#include "api/core/Class.h"
#include "api/core/Convert.h"
#include "api/core/Error.h"
#include "api/globals/Constants.h"
#include "utils/utils.h"

namespace d2bs::runtime::api::classes {

using globals::FileMode;

namespace file_detail {
inline spdlog::logger& Log() {
    static const auto LOGGER = utils::GetLogger("api.file");
    return *LOGGER;
}

// Opens a file under the script folder with _wfopen_s's sharing (_SH_SECURE: a file open for writing is not shared,
// one open for reading shares reads only), throwing "Invalid file name" or "Couldn't open file" and returning nullptr
// when it can't. The fullPath, when given, receives the resolved path.
std::unique_ptr<std::fstream> OpenScriptStream(v8::Isolate* isolate, const std::string& relativePath,
                                               std::ios::openmode mode, std::filesystem::path* fullPath = nullptr);

// Stream operations over the stream's buffer directly, so they keep working once the end of the file is reached (the
// iostream sentries refuse to run with eofbit set). Tell is the position; a successful Seek clears eof; Read returns
// the bytes read and sets eof when it gets fewer than asked; Write returns false, and sets bad, when not every byte was
// written; Flush writes the buffered bytes out.
int64_t Tell(std::fstream& stream);
bool Seek(std::fstream& stream, int64_t offset, std::ios::seekdir dir);
size_t Read(std::fstream& stream, std::span<char> buffer);
bool Write(std::fstream& stream, std::span<const char> bytes);
void Flush(std::fstream& stream);

std::string ReadLine(std::fstream& stream);
bool WriteValue(std::fstream& stream, v8::Isolate* isolate, v8::Local<v8::Value> value, bool isBinary);
size_t SkipBom(std::span<const char> data);
// The file's last access / creation time in seconds since the epoch, 0 when it can't be read. The STL has no access or
// creation time, so these read the CRT's stat.
double AccessedTime(const std::filesystem::path& path);
double CreatedTime(const std::filesystem::path& path);
double ModifiedTime(const std::filesystem::path& path);
}  // namespace file_detail

// Internal file data structure. Owns handle from construction.
struct FileData {
    int32_t mode = 0;                      // File mode (read/write/append) + binary flag (mode > 2 means binary)
    std::filesystem::path path;            // File path relative to scripts folder
    std::filesystem::path fullPath;        // The resolved path, for the file's size and times
    bool autoflush = false;                // Automatically flush after each write
    bool isLocked = false;                 // The lockFile argument of File.open (see open())
    std::unique_ptr<std::fstream> handle;  // The open stream; null while the file is closed

    FileData(int32_t fileMode, std::filesystem::path filePath, std::filesystem::path resolvedPath, bool flush,
             bool locked, std::unique_ptr<std::fstream> stream)
        : mode(fileMode),
          path(std::move(filePath)),
          fullPath(std::move(resolvedPath)),
          autoflush(flush),
          isLocked(locked),
          handle(std::move(stream)) {}
    FileData(const FileData&) = delete;
    FileData& operator=(const FileData&) = delete;
    FileData(FileData&&) = delete;
    FileData& operator=(FileData&&) = delete;

    // NOLINTNEXTLINE(bugprone-exception-escape) - the stream has no exception mask set, so close() reports by failbit
    ~FileData() {
        if (handle) {
            handle->clear();
            handle->close();
            if (handle->fail()) {
                file_detail::Log().warn("Close failed for file: {}", path.string());
            }
        }
    }
};

// JSFile - V8 wrapper for file operations
class JSFile : public ClassBase<JSFile, FileData> {
   public:
    static constexpr std::string_view ClassName = "File";

    // File objects are created via File.open(), not direct construction
    V8_CLASS_NOT_CONSTRUCTABLE

    // Open modes indexed by mode value (0-5): the C modes "rt", "w+t", "a+t", "rb", "w+b" and "a+b".
    static constexpr std::array<std::ios::openmode, 6> MODES = {
        std::ios::in,
        std::ios::in | std::ios::out | std::ios::trunc,
        std::ios::in | std::ios::out | std::ios::app,
        std::ios::in | std::ios::binary,
        std::ios::in | std::ios::out | std::ios::trunc | std::ios::binary,
        std::ios::in | std::ios::out | std::ios::app | std::ios::binary,
    };

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
                auto* data = Unwrap(isolate, self);

                if (!data) {
                    info.GetReturnValue().Set(convert::ToJS(isolate, false));
                    return;
                }

                // File is readable if open, not at EOF, and no errors
                bool readable = data->handle && !data->handle->eof() && !data->handle->bad();
                info.GetReturnValue().Set(convert::ToJS(isolate, readable));
            });
        /// @description True while the file is open in write or append mode and has no pending error.
        /// @type {boolean}
        Property(
            isolate, inst, "writeable",
            +[](v8::Local<v8::Name> property, const v8::PropertyCallbackInfo<v8::Value>& info) {
                auto* isolate = info.GetIsolate();
                auto self = info.Holder();
                auto* data = Unwrap(isolate, self);

                if (!data) {
                    info.GetReturnValue().Set(convert::ToJS(isolate, false));
                    return;
                }

                // File is writeable if open, no errors, and mode is write or append
                bool writeable =
                    data->handle && !data->handle->bad() && (data->mode % 3) > static_cast<int32_t>(FileMode::Read);
                info.GetReturnValue().Set(convert::ToJS(isolate, writeable));
            });
        /// @description True while the file is open and has no pending error.
        /// @type {boolean}
        Property(
            isolate, inst, "seekable",
            +[](v8::Local<v8::Name> property, const v8::PropertyCallbackInfo<v8::Value>& info) {
                auto* isolate = info.GetIsolate();
                auto self = info.Holder();
                auto* data = Unwrap(isolate, self);

                if (!data) {
                    info.GetReturnValue().Set(convert::ToJS(isolate, false));
                    return;
                }

                bool seekable = data->handle && !data->handle->bad();
                info.GetReturnValue().Set(convert::ToJS(isolate, seekable));
            });
        /// @description Base open mode without the binary flag: 0 = read (FILE_READ), 1 = write (FILE_WRITE), 2 =
        /// append (FILE_APPEND).
        /// @type {number}
        Property(
            isolate, inst, "mode", +[](v8::Local<v8::Name> property, const v8::PropertyCallbackInfo<v8::Value>& info) {
                auto* isolate = info.GetIsolate();
                auto self = info.Holder();
                auto* data = Unwrap(isolate, self);

                if (!data) {
                    info.GetReturnValue().Set(convert::ToJS(isolate, 0));
                    return;
                }

                // Return base mode (0, 1, or 2) without binary flag
                info.GetReturnValue().Set(convert::ToJS(isolate, data->mode % 3));
            });
        /// @description True if the file was opened in binary mode, where read/write operate on 32-bit integers rather
        /// than text.
        /// @type {boolean}
        Property(
            isolate, inst, "binaryMode",
            +[](v8::Local<v8::Name> property, const v8::PropertyCallbackInfo<v8::Value>& info) {
                auto* isolate = info.GetIsolate();
                auto self = info.Holder();
                auto* data = Unwrap(isolate, self);

                if (!data) {
                    info.GetReturnValue().Set(convert::ToJS(isolate, false));
                    return;
                }

                // Binary mode if mode > 2 (3, 4, 5 are binary versions of read, write, append)
                info.GetReturnValue().Set(convert::ToJS(isolate, data->mode > 2));
            });
        /// @description Total length of the file in bytes.
        /// @type {number}
        Property(
            isolate, inst, "length",
            +[](v8::Local<v8::Name> property, const v8::PropertyCallbackInfo<v8::Value>& info) {
                auto* isolate = info.GetIsolate();
                auto self = info.Holder();
                auto* data = Unwrap(isolate, self);

                if (!data || !data->handle) {
                    info.GetReturnValue().Set(convert::ToJS(isolate, 0));
                    return;
                }

                // The size on disk: bytes still in the stream's buffer are not counted.
                std::error_code ec;
                const auto size = std::filesystem::file_size(data->fullPath, ec);
                const int32_t length = ec ? -1 : static_cast<int32_t>(size);
                info.GetReturnValue().Set(convert::ToJS(isolate, length));
            });
        /// @description File path as supplied to File.open, relative to the scripts folder.
        /// @type {string}
        Property(
            isolate, inst, "path", +[](v8::Local<v8::Name> property, const v8::PropertyCallbackInfo<v8::Value>& info) {
                auto* isolate = info.GetIsolate();
                auto self = info.Holder();
                auto* data = Unwrap(isolate, self);

                if (!data) {
                    info.GetReturnValue().SetEmptyString();
                    return;
                }

                info.GetReturnValue().Set(convert::ToJS(isolate, data->path));
            });
        /// @description Current read/write position in the file, as a byte offset from the start.
        /// @type {number}
        Property(
            isolate, inst, "position",
            +[](v8::Local<v8::Name> property, const v8::PropertyCallbackInfo<v8::Value>& info) {
                auto* isolate = info.GetIsolate();
                auto self = info.Holder();
                auto* data = Unwrap(isolate, self);

                if (!data || !data->handle) {
                    info.GetReturnValue().Set(convert::ToJS(isolate, 0));
                    return;
                }

                int32_t pos = static_cast<int32_t>(file_detail::Tell(*data->handle));
                info.GetReturnValue().Set(convert::ToJS(isolate, pos));
            });
        /// @description True when the end-of-file indicator is set on the stream.
        /// @type {boolean}
        Property(
            isolate, inst, "eof", +[](v8::Local<v8::Name> property, const v8::PropertyCallbackInfo<v8::Value>& info) {
                auto* isolate = info.GetIsolate();
                auto self = info.Holder();
                auto* data = Unwrap(isolate, self);

                if (!data || !data->handle) {
                    info.GetReturnValue().Set(convert::ToJS(isolate, true));
                    return;
                }

                info.GetReturnValue().Set(convert::ToJS(isolate, data->handle->eof()));
            });
        /// @description Last access time of the file, as a Unix timestamp in seconds since the epoch.
        /// @type {number}
        Property(
            isolate, inst, "accessed",
            +[](v8::Local<v8::Name> property, const v8::PropertyCallbackInfo<v8::Value>& info) {
                auto* isolate = info.GetIsolate();
                auto self = info.Holder();
                auto* data = Unwrap(isolate, self);

                if (!data || !data->handle) {
                    info.GetReturnValue().Set(convert::ToJS(isolate, 0.0));
                    return;
                }

                info.GetReturnValue().Set(convert::ToJS(isolate, file_detail::AccessedTime(data->fullPath)));
            });
        /// @description Creation time of the file, as a Unix timestamp in seconds since the epoch.
        /// @type {number}
        Property(
            isolate, inst, "created",
            +[](v8::Local<v8::Name> property, const v8::PropertyCallbackInfo<v8::Value>& info) {
                auto* isolate = info.GetIsolate();
                auto self = info.Holder();
                auto* data = Unwrap(isolate, self);

                if (!data || !data->handle) {
                    info.GetReturnValue().Set(convert::ToJS(isolate, 0.0));
                    return;
                }

                info.GetReturnValue().Set(convert::ToJS(isolate, file_detail::CreatedTime(data->fullPath)));
            });
        /// @description Last modification time of the file, as a Unix timestamp in seconds since the epoch.
        /// @type {number}
        Property(
            isolate, inst, "modified",
            +[](v8::Local<v8::Name> property, const v8::PropertyCallbackInfo<v8::Value>& info) {
                auto* isolate = info.GetIsolate();
                auto self = info.Holder();
                auto* data = Unwrap(isolate, self);

                if (!data || !data->handle) {
                    info.GetReturnValue().Set(convert::ToJS(isolate, 0.0));
                    return;
                }

                info.GetReturnValue().Set(convert::ToJS(isolate, file_detail::ModifiedTime(data->fullPath)));
            });
        /// @description Whether the stream is flushed to disk automatically after every write() call. Assigned values
        /// are coerced to boolean via JS truthiness.
        /// @type {boolean}
        Property(
            isolate, inst, "autoflush",
            +[](v8::Local<v8::Name> property, const v8::PropertyCallbackInfo<v8::Value>& info) {
                auto* isolate = info.GetIsolate();
                auto self = info.Holder();
                auto* data = Unwrap(isolate, self);

                if (!data) {
                    info.GetReturnValue().Set(convert::ToJS(isolate, false));
                    return;
                }

                info.GetReturnValue().Set(convert::ToJS(isolate, data->autoflush));
            },
            +[](v8::Local<v8::Name> property, v8::Local<v8::Value> value,
                const v8::PropertyCallbackInfo<v8::Boolean>& info) {
                auto* isolate = info.GetIsolate();
                auto self = info.Holder();
                auto* data = Unwrap(isolate, self);

                if (data) {
                    data->autoflush = value->BooleanValue(isolate);
                }
            });

        /// @description Closes the file, so it can later be reopen()ed.
        /// @signature close()
        /// @returns {File} - This file object, for chaining.
        /// @throws {Error} - If the file is not open.
        /// @throws {Error} - If closing the underlying file fails.
        Method(
            isolate, proto, "close", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
                auto* isolate = args.GetIsolate();
                auto self = args.This();
                auto* data = Unwrap(isolate, self);

                if (!data) {
                    error::ThrowError(isolate, "Couldn't get file object");
                    return;
                }

                if (!data->handle) {
                    error::ThrowError(isolate, "File is not open");
                    return;
                }

                data->handle->clear();
                data->handle->close();
                const bool isClosed = !data->handle->fail();
                data->handle.reset();
                if (!isClosed) {
                    error::ThrowError(isolate, "Close failed");
                    return;
                }

                // Return this for chaining
                args.GetReturnValue().Set(self);
            });
        /// @description Reopens a previously closed file using the same path and mode.
        /// @signature reopen()
        /// @returns {File} - This file object, for chaining.
        /// @throws {Error} - If the file is not closed.
        /// @throws {Error} - If the file cannot be opened.
        Method(
            isolate, proto, "reopen", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
                auto* isolate = args.GetIsolate();
                auto self = args.This();
                auto* data = Unwrap(isolate, self);

                if (!data) {
                    error::ThrowError(isolate, "Couldn't get file object");
                    return;
                }

                if (data->handle) {
                    error::ThrowError(isolate, "File is not closed");
                    return;
                }

                // Reopen file with same mode
                std::string pathStr = data->path.string();
                // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index) - mode is 0-5, checked by open()
                data->handle = file_detail::OpenScriptStream(isolate, pathStr, MODES[data->mode], &data->fullPath);
                if (!data->handle) {
                    return;  // OpenScriptStream already threw
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
                auto* data = Unwrap(isolate, self);

                if (!data || !data->handle) {
                    return;  // Return undefined
                }

                if (args.Length() < 1) {
                    error::ThrowError(isolate, "Invalid arguments");
                    return;
                }
                int32_t count = convert::To<int32_t>(isolate, args[0]);
                if (count <= 0) {
                    error::ThrowError(isolate, "Invalid arguments");
                    return;
                }

                data->handle->clear();

                if (data->mode > 2) {
                    // Binary mode: read count ints; a partial last int is not counted.
                    std::vector result(count, 0);
                    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast) - the ints' bytes
                    const std::span bytes(reinterpret_cast<char*>(result.data()), result.size() * sizeof(int32_t));
                    const size_t readCount = file_detail::Read(*data->handle, bytes) / sizeof(int32_t);

                    if (readCount != static_cast<size_t>(count) && data->handle->bad()) {
                        error::ThrowError(isolate, "Read failed");
                        return;
                    }

                    if (count == 1) {
                        args.GetReturnValue().Set(convert::ToJS(isolate, result[0]));
                    } else {
                        auto arr = v8::Array::New(isolate, count);
                        for (int32_t i = 0; i < count; i++) {
                            arr->Set(context, i, convert::ToJS(isolate, result[i])).Check();
                        }
                        args.GetReturnValue().Set(arr);
                    }
                } else {
                    // Text mode
                    bool isBegin = (file_detail::Tell(*data->handle) == 0);

                    file_detail::Flush(*data->handle);

                    std::vector<char> result(count, 0);
                    const size_t readCount = file_detail::Read(*data->handle, result);

                    if (readCount != static_cast<size_t>(count) && data->handle->bad()) {
                        error::ThrowError(isolate, "Read failed");
                        return;
                    }

                    size_t offset = 0;
                    if (isBegin) {
                        offset = file_detail::SkipBom(std::span(result).first(readCount));
                    }

                    args.GetReturnValue().Set(
                        convert::ToJS(isolate, std::string(result.data() + offset, readCount - offset)));
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
                auto* data = Unwrap(isolate, self);

                if (!data || !data->handle) {
                    return;  // Return undefined
                }

                // Reference returns NULL from readLine when at EOF, then throws "Read failed"
                if (data->handle->eof()) {
                    error::ThrowError(isolate, "Read failed");
                    return;
                }

                bool isBegin = (file_detail::Tell(*data->handle) == 0);

                std::string line = file_detail::ReadLine(*data->handle);

                if (isBegin) {
                    size_t offset = file_detail::SkipBom(line);
                    if (offset > 0) {
                        line = line.substr(offset);
                    }
                }

                args.GetReturnValue().Set(convert::ToJS(isolate, line));
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
                auto* data = Unwrap(isolate, self);

                if (!data || !data->handle) {
                    return;  // Return undefined
                }

                auto arr = v8::Array::New(isolate, 0);
                int32_t idx = 0;

                while (true) {
                    if (data->handle->bad()) {
                        error::ThrowError(isolate, "Read failed");
                        return;
                    }
                    if (data->handle->eof())
                        break;

                    bool isBegin = (file_detail::Tell(*data->handle) == 0);

                    std::string line = file_detail::ReadLine(*data->handle);

                    if (data->handle->bad()) {
                        error::ThrowError(isolate, "Read failed");
                        return;
                    }
                    // EOF with no data means we hit EOF without reading new content - don't append
                    if (data->handle->eof() && line.empty())
                        break;

                    if (isBegin) {
                        size_t offset = file_detail::SkipBom(line);
                        if (offset > 0) {
                            line = line.substr(offset);
                        }
                    }

                    arr->Set(context, idx++, convert::ToJS(isolate, line)).Check();
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
                auto* data = Unwrap(isolate, self);

                if (!data || !data->handle) {
                    return;  // Return undefined
                }

                bool isBegin = (file_detail::Tell(*data->handle) == 0);

                // Seek to end to get file size
                file_detail::Seek(*data->handle, 0, std::ios::end);
                int32_t size = static_cast<int32_t>(file_detail::Tell(*data->handle));
                file_detail::Seek(*data->handle, 0, std::ios::beg);

                if (size <= 0) {
                    args.GetReturnValue().Set(convert::ToJS(isolate, ""));
                    return;
                }

                std::vector<char> contents(size, 0);
                const size_t readCount = file_detail::Read(*data->handle, contents);

                if (readCount != static_cast<size_t>(size) && data->handle->bad()) {
                    error::ThrowError(isolate, "Read failed");
                    return;
                }

                size_t offset = 0;
                if (isBegin) {
                    offset = file_detail::SkipBom(std::span(contents).first(readCount));
                }

                args.GetReturnValue().Set(
                    convert::ToJS(isolate, std::string(contents.data() + offset, readCount - offset)));
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
                auto* data = Unwrap(isolate, self);

                // Reference silently returns this when file is not open
                if (data && data->handle) {
                    bool isBinary = data->mode > 2;

                    // Process each argument and write to file
                    for (int32_t i = 0; i < args.Length(); i++) {
                        file_detail::WriteValue(*data->handle, isolate, args[i], isBinary);
                    }

                    if (data->autoflush) {
                        file_detail::Flush(*data->handle);
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
                auto* data = Unwrap(isolate, self);

                if (!data || !data->handle) {
                    error::ThrowError(isolate, "File is not open");
                    return;
                }

                if (args.Length() < 1) {
                    error::ThrowError(isolate, "Not enough parameters");
                    return;
                }

                int32_t offset = convert::To<int32_t>(isolate, args[0]);
                bool isLines = args.Length() > 1 ? convert::To<bool>(isolate, args[1]) : false;
                bool fromStart = args.Length() > 2 ? convert::To<bool>(isolate, args[2]) : false;

                if (fromStart) {
                    file_detail::Seek(*data->handle, 0, std::ios::beg);
                    data->handle->clear();  // rewind() equivalent: seek + clear error
                }

                if (!isLines) {
                    if (!file_detail::Seek(*data->handle, offset, std::ios::cur)) {
                        error::ThrowError(isolate, "Seek failed");
                        return;
                    }
                } else {
                    // Seek by lines: read and discard 'offset' lines
                    for (int32_t i = 0; i < offset; i++) {
                        file_detail::ReadLine(*data->handle);
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
                auto* data = Unwrap(args.GetIsolate(), self);

                if (data && data->handle) {
                    file_detail::Flush(*data->handle);
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
                auto* data = Unwrap(isolate, self);

                if (data && data->handle) {
                    if (!file_detail::Seek(*data->handle, 0, std::ios::beg)) {
                        error::ThrowError(isolate, "Seek failed");
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
                auto* data = Unwrap(isolate, self);

                if (data && data->handle) {
                    if (!file_detail::Seek(*data->handle, 0, std::ios::end)) {
                        error::ThrowError(isolate, "Seek failed");
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
        /// @param lockFile {boolean} - Optional, default false. Accepted for compatibility; it has no effect (the C
        /// runtime stream lock it took only ever held off other threads using this same File, which no script can).
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
                    error::ThrowError(isolate, "Not enough parameters, 2 or more expected");
                    return;
                }

                if (!args[0]->IsString()) {
                    error::ThrowError(isolate, "Parameter 1 must be a string (path)");
                    return;
                }

                if (!args[1]->IsNumber()) {
                    error::ThrowError(isolate, "Parameter 2 must be a number (mode)");
                    return;
                }

                std::string path = convert::ToString(isolate, args[0]);
                int32_t mode = convert::To<int32_t>(isolate, args[1]);
                bool binary = args.Length() > 2 ? convert::To<bool>(isolate, args[2]) : false;
                bool autoflush = args.Length() > 3 ? convert::To<bool>(isolate, args[3]) : false;
                bool lockFile = args.Length() > 4 ? convert::To<bool>(isolate, args[4]) : false;

                // Validate path
                if (path.empty()) {
                    error::ThrowError(isolate, "Invalid file name");
                    return;
                }

                // Validate mode
                if (mode < static_cast<int32_t>(FileMode::Read) || mode > static_cast<int32_t>(FileMode::Append)) {
                    error::ThrowError(isolate, "Invalid file mode");
                    return;
                }

                // Adjust mode for binary
                if (binary) {
                    mode += 3;
                }

                // Open the file
                std::filesystem::path fullPath;
                // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index) - mode is 0-5, checked above
                auto stream = file_detail::OpenScriptStream(isolate, path, MODES[mode], &fullPath);
                if (!stream) {
                    return;  // OpenScriptStream already threw
                }

                // Create File object via CreateInstance (bypasses NOT_CONSTRUCTABLE guard). A failure leaves the
                // stream with the native that was never built, which closes it.
                auto obj = CreateInstance(isolate, context, mode, std::filesystem::path(path), std::move(fullPath),
                                          autoflush, lockFile, std::move(stream));
                if (obj.IsEmpty()) {
                    return;
                }

                args.GetReturnValue().Set(obj);
            });
    }
};

}  // namespace d2bs::runtime::api::classes
