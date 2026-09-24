#include "JSHttpClient.h"

#include <cstddef>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "api/core/Convert.h"
#include "api/core/Error.h"
#include "http/Client.h"

namespace d2bs::api::classes {

namespace {

// Extract a request body from a string, an ArrayBuffer, or a typed array / DataView. Returns false
// (and throws a TypeError) for any other value.
bool ExtractBody(const ub::Context& context, const ub::Local<ub::Value>& value, std::vector<uint8_t>& out) {
    if (value.IsString()) {
        std::string text = convert::ToString(context, value);
        out.assign(text.begin(), text.end());
        return true;
    }
    if (auto buffer = value.To<ub::ArrayBuffer>()) {
        out.resize(ub::ByteLength(*buffer));
        out.resize(ub::CopyBytes(*buffer, std::as_writable_bytes(std::span(out))));
        return true;
    }
    if (auto view = value.To<ub::ArrayBufferView>()) {
        out.resize(ub::ByteLength(*view));
        out.resize(ub::CopyBytes(*view, std::as_writable_bytes(std::span(out))));
        return true;
    }
    error::ThrowTypeError(context.GetIsolate(), "body must be a string, ArrayBuffer, or typed array");
    return false;
}

// The small option readers below share these semantics: a missing or `undefined`
// option is left at its default; a present-but-wrong-typed option is rejected with
// a TypeError; an engine error while reading propagates. Each returns false with a
// pending exception on rejection/error (the "bail now" idiom), true otherwise.

bool ReadStringOption(const ub::Context& context, const ub::Local<ub::Object>& options, std::string_view name,
                      std::string& out) {
    auto value = options.Get(context, name);
    if (!value) {
        return false;
    }
    if (value->IsUndefined()) {
        return true;
    }
    if (!value->IsString()) {
        error::ThrowTypeError(context.GetIsolate(), std::format("HttpClient: '{}' must be a string", name));
        return false;
    }
    out = convert::ToString(context, *value);
    return true;
}

bool ReadUint32Option(const ub::Context& context, const ub::Local<ub::Object>& options, std::string_view name,
                      uint32_t& out) {
    auto value = options.Get(context, name);
    if (!value) {
        return false;
    }
    if (value->IsUndefined()) {
        return true;
    }
    if (!value->IsNumber()) {
        error::ThrowTypeError(context.GetIsolate(), std::format("HttpClient: '{}' must be a number", name));
        return false;
    }
    double number = convert::ToDouble(context, *value);
    if (number < 0) {
        error::ThrowRangeError(context.GetIsolate(), std::format("HttpClient: '{}' must not be negative", name));
        return false;
    }
    out = static_cast<uint32_t>(number);
    return true;
}

bool ReadBoolOption(const ub::Context& context, const ub::Local<ub::Object>& options, std::string_view name,
                    bool& out) {
    auto value = options.Get(context, name);
    if (!value) {
        return false;
    }
    if (value->IsUndefined()) {
        return true;
    }
    if (!value->IsBoolean()) {
        error::ThrowTypeError(context.GetIsolate(), std::format("HttpClient: '{}' must be a boolean", name));
        return false;
    }
    out = value->IsTrue();
    return true;
}

// Read the request headers out of `headers` (already known to be present). Returns false with a
// pending exception on rejection / engine error.
bool ReadHeaders(const ub::Context& context, const ub::Local<ub::Value>& headers, http::Request& request) {
    auto headerObject = headers.To<ub::Object>();
    if (!headerObject) {
        error::ThrowTypeError(context.GetIsolate(), "HttpClient: 'headers' must be an object");
        return false;
    }
    auto names = headerObject->GetOwnPropertyNames(context);
    if (!names) {
        return false;
    }
    for (uint32_t i = 0; i < names->Length(); ++i) {
        auto key = names->Get(context, i);
        if (!key) {
            return false;
        }
        // An index-like key may come back as a number; property lookup wants a name.
        auto keyName = key->ToString(context);
        if (!keyName) {
            return false;
        }
        auto value = headerObject->Get(context, *keyName);
        if (!value) {
            return false;
        }
        std::string headerName = keyName->Utf8Value();
        if (!value->IsString() && !value->IsNumber()) {
            error::ThrowTypeError(
                context.GetIsolate(),
                std::format("HttpClient: header '{}' must have a string or number value", headerName));
            return false;
        }
        request.headers.emplace_back(std::move(headerName), convert::ToString(context, *value));
    }
    return true;
}

// Read recognized fields out of the JS options object into `request`. Wrong-typed
// options are rejected (not silently ignored). Returns false with a pending
// exception on any rejection / engine error; true otherwise.
bool ApplyOptions(const ub::Context& context, const ub::Local<ub::Object>& options, http::Request& request,
                  bool allowMethod, bool allowBody, bool& binary) {
    if (allowMethod && !ReadStringOption(context, options, "method", request.method)) {
        return false;
    }

    auto headers = options.Get(context, "headers");
    if (!headers) {
        return false;
    }
    if (!headers->IsUndefined() && !ReadHeaders(context, *headers, request)) {
        return false;
    }

    if (allowBody) {
        auto body = options.Get(context, "body");
        if (!body) {
            return false;
        }
        if (!body->IsNullOrUndefined() && !ExtractBody(context, *body, request.body)) {
            return false;
        }
    }

    if (!ReadUint32Option(context, options, "timeout", request.timeoutMs)) {
        return false;
    }
    if (!ReadUint32Option(context, options, "totalTimeout", request.totalTimeoutMs)) {
        return false;
    }
    if (!ReadBoolOption(context, options, "followRedirects", request.followRedirects)) {
        return false;
    }
    if (!ReadBoolOption(context, options, "insecure", request.insecure)) {
        return false;
    }
    if (!ReadBoolOption(context, options, "binary", binary)) {
        return false;
    }

    auto maxBytes = options.Get(context, "maxResponseBytes");
    if (!maxBytes) {
        return false;
    }
    if (!maxBytes->IsUndefined()) {
        if (!maxBytes->IsNumber()) {
            error::ThrowTypeError(context.GetIsolate(), "HttpClient: 'maxResponseBytes' must be a number");
            return false;
        }
        double bytes = convert::ToDouble(context, *maxBytes);
        if (bytes <= 0) {
            error::ThrowRangeError(context.GetIsolate(), "HttpClient: 'maxResponseBytes' must be positive");
            return false;
        }
        request.maxResponseBytes = static_cast<size_t>(bytes);
    }

    return true;
}

// Build the plain JS response object returned to scripts. Empty if the engine failed part way.
std::optional<ub::Local<ub::Object>> BuildResponseObject(const ub::Context& context, const http::Response& response,
                                                         bool binary) {
    auto& isolate = context.GetIsolate();
    auto object = ub::Object::New(context);
    if (!object) {
        return std::nullopt;
    }

    auto statusText = ub::String::NewFromUtf8(isolate, response.statusText);
    auto url = ub::String::NewFromUtf8(isolate, response.url);
    if (!statusText || !url) {
        return std::nullopt;
    }
    const bool isSet =
        object->Set(context, "status", convert::ToJS(isolate, response.status)).value_or(false) &&
        object->Set(context, "statusText", *statusText).value_or(false) &&
        object->Set(context, "ok", convert::ToJS(isolate, response.status >= 200 && response.status < 300))
            .value_or(false) &&
        object->Set(context, "url", *url).value_or(false);
    if (!isSet) {
        return std::nullopt;
    }

    auto headerObject = ub::Object::New(context);
    if (!headerObject) {
        return std::nullopt;
    }
    for (const auto& [name, value] : response.headers) {
        auto key = ub::String::NewFromUtf8(isolate, name);
        auto text = ub::String::NewFromUtf8(isolate, value);
        if (!key || !text || !headerObject->Set(context, *key, *text).value_or(false)) {
            return std::nullopt;
        }
    }
    if (!object->Set(context, "headers", *headerObject).value_or(false)) {
        return std::nullopt;
    }

    std::optional<ub::Local<ub::Value>> body;
    if (binary) {
        body = ub::ArrayBuffer::New(context, std::as_bytes(std::span(response.body)));
    } else {
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast) - byte buffer viewed as chars
        std::string_view text(reinterpret_cast<const char*>(response.body.data()), response.body.size());
        body = ub::String::NewFromUtf8(isolate, text);
    }
    if (!body || !object->Set(context, "body", *body).value_or(false)) {
        return std::nullopt;
    }

    return object;
}

// Shared driver for every static method.
// - urlInOptions:    true for request(url is options.url); false for url-first helpers.
// - bodyArgIndex:    index of a positional body argument, or -1 (body comes from options.body).
// - optionsArgIndex: index of the options object for the url-first helpers.
void RequestImpl(const ub::CallbackInfo& args, std::string_view defaultMethod, bool urlInOptions, int32_t bodyArgIndex,
                 int32_t optionsArgIndex) {
    auto& isolate = args.GetIsolate();
    const auto& context = args.GetContext();

    http::Request request;
    request.method = std::string(defaultMethod);
    bool binary = false;

    std::optional<ub::Local<ub::Object>> options;

    if (urlInOptions) {
        options = args[0].To<ub::Object>();
        if (args.Length() < 1 || !options) {
            error::ThrowTypeError(isolate, "HttpClient.request requires an options object");
            return;
        }
        auto url = options->Get(context, "url");
        if (!url) {
            return;
        }
        if (!url->IsString()) {
            error::ThrowTypeError(isolate, "HttpClient.request options must include a url string");
            return;
        }
        request.url = convert::ToString(context, *url);
    } else {
        if (args.Length() < 1 || !args[0].IsString()) {
            error::ThrowTypeError(isolate, "url must be a string");
            return;
        }
        request.url = convert::ToString(context, args[0]);
        // A present-but-non-object options argument is a mistake, not a no-op.
        if (optionsArgIndex >= 0 && args.Length() > static_cast<uint32_t>(optionsArgIndex) &&
            !args[optionsArgIndex].IsNullOrUndefined()) {
            options = args[optionsArgIndex].To<ub::Object>();
            if (!options) {
                error::ThrowTypeError(isolate, "options must be an object");
                return;
            }
        }
    }

    bool havePositionalBody = false;
    if (bodyArgIndex >= 0 && args.Length() > static_cast<uint32_t>(bodyArgIndex) &&
        !args[bodyArgIndex].IsNullOrUndefined()) {
        if (!ExtractBody(context, args[bodyArgIndex], request.body)) {
            return;
        }
        havePositionalBody = true;
    }

    if (options && !ApplyOptions(context, *options, request, urlInOptions, !havePositionalBody, binary)) {
        return;
    }

    http::Response response;
    std::string failure = http::Perform(request, response);
    if (!failure.empty()) {
        error::ThrowError(isolate, "HTTP request failed: " + failure);
        return;
    }

    if (auto object = BuildResponseObject(context, response, binary)) {
        args.GetReturnValue().Set(*object);
    }
}

}  // namespace

void JSHttpClient::Configure(const ub::Class<HttpClientData>& cls) {
    /// @description Performs a blocking HTTP/HTTPS request described by an options object. This is the general form;
    /// get/post/put/delete/head are thin wrappers over it.
    /// @signature request(options: object)
    /// @param options {object} - request descriptor: url {string} (required), method {string} (default "GET"), headers
    /// {object} of name->value, body {string|ArrayBuffer|TypedArray}, timeout {number} per-operation ms (default
    /// 30000), totalTimeout {number} overall wall-clock ms across the whole request including the body read (default
    /// 120000; 0 disables), followRedirects {boolean} (default true), insecure {boolean} (default false; true disables
    /// TLS certificate/hostname validation - dangerous, MITM-able), binary {boolean} (default false; when true
    /// response.body is an ArrayBuffer), maxResponseBytes {number} (default 64 MiB). A present-but-wrong-typed option
    /// is rejected, not ignored.
    /// @returns {object} - { status: number, statusText: string, ok: boolean, headers: object, url: string, body:
    /// string|ArrayBuffer }. Non-2xx responses are returned, not thrown.
    /// @throws {TypeError} - if options is not an object, url is absent or not a string, or any option (method,
    /// headers, header values, body, timeout, totalTimeout, followRedirects, insecure, binary, maxResponseBytes) has
    /// the wrong type.
    /// @throws {RangeError} - if timeout/totalTimeout is negative, or maxResponseBytes is not positive.
    /// @throws {Error} - on transport failure (DNS, TLS, connection refused, per-op or total timeout, oversized body).
    StaticMethod(
        cls, "request", +[](const ub::CallbackInfo& args) {
            RequestImpl(args, "GET", /*urlInOptions=*/true, /*bodyArgIndex=*/-1, /*optionsArgIndex=*/-1);
        });

    /// @description Performs a blocking HTTP GET request.
    /// @signature get(url: string, options?: object)
    /// @param url {string} - the absolute URL to request (http or https).
    /// @param options {object} - optional request options (headers, timeout, totalTimeout, followRedirects, insecure,
    /// binary, maxResponseBytes); see request(). A present-but-non-object value is rejected.
    /// @returns {object} - the response object; see request().
    /// @throws {TypeError} - if url is not a string, options is not an object, or an option has the wrong type.
    /// @throws {Error} - on transport failure.
    StaticMethod(
        cls, "get", +[](const ub::CallbackInfo& args) {
            RequestImpl(args, "GET", /*urlInOptions=*/false, /*bodyArgIndex=*/-1, /*optionsArgIndex=*/1);
        });

    /// @description Performs a blocking HTTP HEAD request (response has no body).
    /// @signature head(url: string, options?: object)
    /// @param url {string} - the absolute URL to request.
    /// @param options {object} - optional request options; see request(). A present-but-non-object value is rejected.
    /// @returns {object} - the response object (body is an empty string); see request().
    /// @throws {TypeError} - if url is not a string, options is not an object, or an option has the wrong type.
    /// @throws {Error} - on transport failure.
    StaticMethod(
        cls, "head", +[](const ub::CallbackInfo& args) {
            RequestImpl(args, "HEAD", /*urlInOptions=*/false, /*bodyArgIndex=*/-1, /*optionsArgIndex=*/1);
        });

    /// @description Performs a blocking HTTP DELETE request.
    /// @signature delete(url: string, options?: object)
    /// @param url {string} - the absolute URL to request.
    /// @param options {object} - optional request options (may include body); see request(). A present-but-non-object
    /// value is rejected.
    /// @returns {object} - the response object; see request().
    /// @throws {TypeError} - if url is not a string, options is not an object, or an option has the wrong type.
    /// @throws {Error} - on transport failure.
    StaticMethod(
        cls, "delete", +[](const ub::CallbackInfo& args) {
            RequestImpl(args, "DELETE", /*urlInOptions=*/false, /*bodyArgIndex=*/-1, /*optionsArgIndex=*/1);
        });

    /// @description Performs a blocking HTTP POST request with the given body.
    /// @signature post(url: string, body?: string|ArrayBuffer|TypedArray, options?: object)
    /// @param url {string} - the absolute URL to request.
    /// @param body {string|ArrayBuffer|TypedArray} - the request body; omit/null for none. Set a Content-Type header
    /// yourself via options.headers.
    /// @param options {object} - optional request options; see request(). A positional body overrides options.body.
    /// @returns {object} - the response object; see request().
    /// @throws {TypeError} - if url is not a string, body has an unusable type, options is not an object, or an option
    /// has the wrong type.
    /// @throws {Error} - on transport failure.
    StaticMethod(
        cls, "post", +[](const ub::CallbackInfo& args) {
            RequestImpl(args, "POST", /*urlInOptions=*/false, /*bodyArgIndex=*/1, /*optionsArgIndex=*/2);
        });

    /// @description Performs a blocking HTTP PUT request with the given body.
    /// @signature put(url: string, body?: string|ArrayBuffer|TypedArray, options?: object)
    /// @param url {string} - the absolute URL to request.
    /// @param body {string|ArrayBuffer|TypedArray} - the request body; omit/null for none.
    /// @param options {object} - optional request options; see request(). A positional body overrides options.body.
    /// @returns {object} - the response object; see request().
    /// @throws {TypeError} - if url is not a string, body has an unusable type, options is not an object, or an option
    /// has the wrong type.
    /// @throws {Error} - on transport failure.
    StaticMethod(
        cls, "put", +[](const ub::CallbackInfo& args) {
            RequestImpl(args, "PUT", /*urlInOptions=*/false, /*bodyArgIndex=*/1, /*optionsArgIndex=*/2);
        });
}

}  // namespace d2bs::api::classes
