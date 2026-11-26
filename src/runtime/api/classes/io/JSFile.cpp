#include "JSFile.h"

#include <share.h>
#include <sys/stat.h>

#include <chrono>
#include <string>
#include <string_view>

#include <fmt/format.h>

#include "api/core/Convert.h"
#include "api/core/Error.h"
#include "config/AppConfig.h"

namespace d2bs::runtime::api::classes::file_detail {

std::unique_ptr<std::fstream> OpenScriptStream(v8::Isolate* isolate, const std::string& relativePath,
                                               std::ios::openmode mode, std::filesystem::path* fullPath) {
    auto resolved = core::config::GetPathRelScript(relativePath);
    if (resolved.empty()) {
        error::ThrowError(isolate, "Invalid file name");
        return nullptr;
    }
    // MSVC's fstream takes the sharing of _wfsopen as its third argument; _SH_SECURE is what _wfopen_s used.
    auto stream = std::make_unique<std::fstream>(resolved.c_str(), mode, _SH_SECURE);
    if (!stream->is_open()) {
        error::ThrowError(isolate, "Couldn't open file");
        return nullptr;
    }
    if (fullPath != nullptr) {
        *fullPath = std::move(resolved);
    }
    return stream;
}

int64_t Tell(std::fstream& stream) {
    return stream.rdbuf()->pubseekoff(0, std::ios::cur, std::ios::in | std::ios::out);
}

bool Seek(std::fstream& stream, int64_t offset, std::ios::seekdir dir) {
    if (stream.rdbuf()->pubseekoff(offset, dir, std::ios::in | std::ios::out) == std::streampos(-1)) {
        return false;
    }
    stream.clear(stream.rdstate() & ~std::ios::eofbit);
    return true;
}

size_t Read(std::fstream& stream, std::span<char> buffer) {
    const auto count =
        static_cast<size_t>(stream.rdbuf()->sgetn(buffer.data(), static_cast<std::streamsize>(buffer.size())));
    if (count < buffer.size()) {
        stream.setstate(std::ios::eofbit);
    }
    return count;
}

bool Write(std::fstream& stream, std::span<const char> bytes) {
    const auto count = stream.rdbuf()->sputn(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    if (std::cmp_not_equal(count, bytes.size())) {
        stream.setstate(std::ios::badbit);
        return false;
    }
    return true;
}

void Flush(std::fstream& stream) {
    stream.rdbuf()->pubsync();
}

std::string ReadLine(std::fstream& stream) {
    if (stream.eof()) {
        return "";
    }
    std::string buffer;
    while (true) {
        const auto ch = stream.rdbuf()->sbumpc();
        if (std::fstream::traits_type::eq_int_type(ch, std::fstream::traits_type::eof())) {
            stream.setstate(std::ios::eofbit);
            break;
        }
        if (ch == '\n') {
            break;
        }
        if (ch != '\r') {
            buffer += std::fstream::traits_type::to_char_type(ch);
        }
    }
    return buffer;
}

namespace {

template <typename T>
bool WriteRaw(std::fstream& stream, const T& value) {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast) - the value's bytes
    return Write(stream, std::span(reinterpret_cast<const char*>(&value), sizeof(T)));
}

}  // namespace

bool WriteValue(std::fstream& stream, v8::Isolate* isolate, v8::Local<v8::Value> value, bool isBinary) {
    if (value->IsNullOrUndefined()) {
        return WriteRaw(stream, int32_t{0});
    }

    if (value->IsString()) {
        return Write(stream, convert::ToString(isolate, value));
    }

    if (value->IsNumber()) {
        auto context = isolate->GetCurrentContext();
        if (isBinary) {
            if (value->IsInt32()) {
                return WriteRaw(stream, value->Int32Value(context).FromMaybe(0));
            }
            return WriteRaw(stream, value->NumberValue(context).FromMaybe(0.0));
        }
        // Text mode
        if (value->IsInt32()) {
            return Write(stream, fmt::format("{}", value->Int32Value(context).FromMaybe(0)));
        }
        return Write(stream, fmt::format("{:.16f}", value->NumberValue(context).FromMaybe(0.0)));
    }

    if (value->IsBoolean()) {
        bool bval = value->BooleanValue(isolate);
        if (isBinary) {
            return WriteRaw(stream, bval);
        }
        return Write(stream, std::string_view(bval ? "true" : "false"));
    }

    return false;
}

size_t SkipBom(std::span<const char> data) {
    if (data.size() >= 3 && data[0] == '\xEF' && data[1] == '\xBB' && data[2] == '\xBF') {
        return 3;
    }
    return 0;
}

namespace {

struct _stat64 StatOf(const std::filesystem::path& path) {
    struct _stat64 fileStat = {};
    _wstat64(path.c_str(), &fileStat);
    return fileStat;
}

}  // namespace

double AccessedTime(const std::filesystem::path& path) {
    return static_cast<double>(StatOf(path).st_atime);
}

double CreatedTime(const std::filesystem::path& path) {
    return static_cast<double>(StatOf(path).st_ctime);
}

double ModifiedTime(const std::filesystem::path& path) {
    std::error_code ec;
    const auto written = std::filesystem::last_write_time(path, ec);
    if (ec) {
        return 0.0;
    }
    const auto seconds =
        std::chrono::floor<std::chrono::seconds>(std::chrono::clock_cast<std::chrono::system_clock>(written));
    return static_cast<double>(seconds.time_since_epoch().count());
}

}  // namespace d2bs::runtime::api::classes::file_detail
