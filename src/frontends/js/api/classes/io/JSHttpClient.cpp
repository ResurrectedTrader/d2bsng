#include "JSHttpClient.h"

#include <format>
#include <string>
#include <string_view>
#include <vector>

#include "HttpEngine.h"

namespace d2bs::api::classes {

namespace {

// Extract a request body from a string, ArrayBuffer, or typed array / DataView.
// Returns false (and throws a TypeError) for any other value.
bool ExtractBody(v8::Isolate* isolate, v8::Local<v8::Value> value, std::vector<uint8_t>& out) {
    if (value->IsString()) {
        std::string text = v8_convert::ToString(isolate, value);
        out.assign(text.begin(), text.end());
        return true;
    }
    if (value->IsArrayBuffer()) {
        auto store = value.As<v8::ArrayBuffer>()->GetBackingStore();
        out.resize(store->ByteLength());
        if (!out.empty()) {
            std::memcpy(out.data(), store->Data(), out.size());
        }
        return true;
    }
    if (value->IsArrayBufferView()) {  // typed array or DataView
        auto view = value.As<v8::ArrayBufferView>();
        auto store = view->Buffer()->GetBackingStore();
        out.resize(view->ByteLength());
        if (!out.empty()) {
            std::memcpy(out.data(), static_cast<const uint8_t*>(store->Data()) + view->ByteOffset(), out.size());
        }
        return true;
    }
    v8_error::ThrowTypeError(isolate, "body must be a string, ArrayBuffer, or typed array");
    return false;
}

// The small option readers below share these semantics: a missing or `undefined`
// option is left at its default; a present-but-wrong-typed option is rejected with
// a TypeError; a V8 error while reading propagates. Each returns false with a
// pending exception on rejection/error (the V8 "bail now" idiom), true otherwise.

bool ReadStringOption(v8::Isolate* isolate, v8::Local<v8::Context> context, v8::Local<v8::Object> options,
                      std::string_view name, std::string& out) {
    v8::Local<v8::Value> value;
    if (!options->Get(context, v8_convert::ToV8(isolate, name)).ToLocal(&value)) {
        return false;
    }
    if (value->IsUndefined()) {
        return true;
    }
    if (!value->IsString()) {
        v8_error::ThrowTypeError(isolate, std::format("HttpClient: '{}' must be a string", name));
        return false;
    }
    out = v8_convert::ToString(isolate, value);
    return true;
}

bool ReadUint32Option(v8::Isolate* isolate, v8::Local<v8::Context> context, v8::Local<v8::Object> options,
                      std::string_view name, uint32_t& out) {
    v8::Local<v8::Value> value;
    if (!options->Get(context, v8_convert::ToV8(isolate, name)).ToLocal(&value)) {
        return false;
    }
    if (value->IsUndefined()) {
        return true;
    }
    if (!value->IsNumber()) {
        v8_error::ThrowTypeError(isolate, std::format("HttpClient: '{}' must be a number", name));
        return false;
    }
    double number = v8_convert::ToDouble(isolate, value);
    if (number < 0) {
        v8_error::ThrowRangeError(isolate, std::format("HttpClient: '{}' must not be negative", name));
        return false;
    }
    out = static_cast<uint32_t>(number);
    return true;
}

bool ReadBoolOption(v8::Isolate* isolate, v8::Local<v8::Context> context, v8::Local<v8::Object> options,
                    std::string_view name, bool& out) {
    v8::Local<v8::Value> value;
    if (!options->Get(context, v8_convert::ToV8(isolate, name)).ToLocal(&value)) {
        return false;
    }
    if (value->IsUndefined()) {
        return true;
    }
    if (!value->IsBoolean()) {
        v8_error::ThrowTypeError(isolate, std::format("HttpClient: '{}' must be a boolean", name));
        return false;
    }
    out = v8_convert::ToBool(isolate, value);
    return true;
}

// Read recognized fields out of the JS options object into `request`. Wrong-typed
// options are rejected (not silently ignored). Returns false with a pending
// exception on any rejection / V8 error; true otherwise.
bool ApplyOptions(v8::Isolate* isolate, v8::Local<v8::Context> context, v8::Local<v8::Object> options,
                  HttpRequest& request, bool allowMethod, bool allowBody, bool& binary) {
    if (allowMethod && !ReadStringOption(isolate, context, options, "method", request.method)) {
        return false;
    }

    v8::Local<v8::Value> headers;
    if (!options->Get(context, v8_convert::ToV8(isolate, "headers")).ToLocal(&headers)) {
        return false;
    }
    if (!headers->IsUndefined()) {
        if (!headers->IsObject()) {
            v8_error::ThrowTypeError(isolate, "HttpClient: 'headers' must be an object");
            return false;
        }
        auto headerObject = headers.As<v8::Object>();
        v8::Local<v8::Array> names;
        if (!headerObject->GetOwnPropertyNames(context).ToLocal(&names)) {
            return false;
        }
        for (uint32_t i = 0; i < names->Length(); ++i) {
            v8::Local<v8::Value> key;
            if (!names->Get(context, i).ToLocal(&key)) {
                return false;
            }
            v8::Local<v8::Value> value;
            if (!headerObject->Get(context, key).ToLocal(&value)) {
                return false;
            }
            if (!value->IsString() && !value->IsNumber()) {
                v8_error::ThrowTypeError(isolate,
                                         std::format("HttpClient: header '{}' must have a string or number value",
                                                     v8_convert::ToString(isolate, key)));
                return false;
            }
            request.headers.emplace_back(v8_convert::ToString(isolate, key), v8_convert::ToString(isolate, value));
        }
    }

    if (allowBody) {
        v8::Local<v8::Value> body;
        if (!options->Get(context, v8_convert::ToV8(isolate, "body")).ToLocal(&body)) {
            return false;
        }
        if (!body->IsNullOrUndefined() && !ExtractBody(isolate, body, request.body)) {
            return false;
        }
    }

    if (!ReadUint32Option(isolate, context, options, "timeout", request.timeoutMs)) {
        return false;
    }
    if (!ReadUint32Option(isolate, context, options, "totalTimeout", request.totalTimeoutMs)) {
        return false;
    }
    if (!ReadBoolOption(isolate, context, options, "followRedirects", request.followRedirects)) {
        return false;
    }
    if (!ReadBoolOption(isolate, context, options, "insecure", request.insecure)) {
        return false;
    }
    if (!ReadBoolOption(isolate, context, options, "binary", binary)) {
        return false;
    }

    v8::Local<v8::Value> maxBytes;
    if (!options->Get(context, v8_convert::ToV8(isolate, "maxResponseBytes")).ToLocal(&maxBytes)) {
        return false;
    }
    if (!maxBytes->IsUndefined()) {
        if (!maxBytes->IsNumber()) {
            v8_error::ThrowTypeError(isolate, "HttpClient: 'maxResponseBytes' must be a number");
            return false;
        }
        double bytes = v8_convert::ToDouble(isolate, maxBytes);
        if (bytes <= 0) {
            v8_error::ThrowRangeError(isolate, "HttpClient: 'maxResponseBytes' must be positive");
            return false;
        }
        request.maxResponseBytes = static_cast<size_t>(bytes);
    }

    return true;
}

// Build the plain JS response object returned to scripts.
v8::Local<v8::Value> BuildResponseObject(v8::Isolate* isolate, v8::Local<v8::Context> context,
                                         const HttpResponse& response, bool binary) {
    v8::EscapableHandleScope scope(isolate);
    auto object = v8::Object::New(isolate);

    object->Set(context, v8_convert::ToV8(isolate, "status"), v8_convert::ToV8(isolate, response.status)).Check();
    object->Set(context, v8_convert::ToV8(isolate, "statusText"), v8_convert::ToV8(isolate, response.statusText))
        .Check();
    object
        ->Set(context, v8_convert::ToV8(isolate, "ok"),
              v8_convert::ToV8(isolate, response.status >= 200 && response.status < 300))
        .Check();
    object->Set(context, v8_convert::ToV8(isolate, "url"), v8_convert::ToV8(isolate, response.url)).Check();

    auto headerObject = v8::Object::New(isolate);
    for (const auto& [name, value] : response.headers) {
        headerObject->Set(context, v8_convert::ToV8(isolate, name), v8_convert::ToV8(isolate, value)).Check();
    }
    object->Set(context, v8_convert::ToV8(isolate, "headers"), headerObject).Check();

    if (binary) {
        auto buffer = v8::ArrayBuffer::New(isolate, response.body.size());
        if (!response.body.empty()) {
            std::memcpy(buffer->GetBackingStore()->Data(), response.body.data(), response.body.size());
        }
        object->Set(context, v8_convert::ToV8(isolate, "body"), buffer).Check();
    } else {
        // View the body bytes as UTF-8 directly; ToV8 copies them into the V8 heap, so an
        // owning std::string here would just be a wasted full-size copy.
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast) - byte buffer viewed as chars
        std::string_view text(reinterpret_cast<const char*>(response.body.data()), response.body.size());
        object->Set(context, v8_convert::ToV8(isolate, "body"), v8_convert::ToV8(isolate, text)).Check();
    }

    return scope.Escape(object);
}

// Shared driver for every static method.
// - urlInOptions:    true for request(url is options.url); false for url-first helpers.
// - bodyArgIndex:    index of a positional body argument, or -1 (body comes from options.body).
// - optionsArgIndex: index of the options object for the url-first helpers.
void RequestImpl(const v8::FunctionCallbackInfo<v8::Value>& args, std::string_view defaultMethod, bool urlInOptions,
                 int bodyArgIndex, int optionsArgIndex) {
    auto* isolate = args.GetIsolate();
    auto context = isolate->GetCurrentContext();

    HttpRequest request;
    request.method = std::string(defaultMethod);
    bool binary = false;

    v8::Local<v8::Object> options;
    bool haveOptions = false;

    if (urlInOptions) {
        if (args.Length() < 1 || !args[0]->IsObject()) {
            v8_error::ThrowTypeError(isolate, "HttpClient.request requires an options object");
            return;
        }
        options = args[0].As<v8::Object>();
        haveOptions = true;
        v8::Local<v8::Value> url;
        if (!options->Get(context, v8_convert::ToV8(isolate, "url")).ToLocal(&url)) {
            return;
        }
        if (!url->IsString()) {
            v8_error::ThrowTypeError(isolate, "HttpClient.request options must include a url string");
            return;
        }
        request.url = v8_convert::ToString(isolate, url);
    } else {
        if (args.Length() < 1 || !args[0]->IsString()) {
            v8_error::ThrowTypeError(isolate, "url must be a string");
            return;
        }
        request.url = v8_convert::ToString(isolate, args[0]);
        // A present-but-non-object options argument is a mistake, not a no-op.
        if (optionsArgIndex >= 0 && args.Length() > optionsArgIndex && !args[optionsArgIndex]->IsNullOrUndefined()) {
            if (!args[optionsArgIndex]->IsObject()) {
                v8_error::ThrowTypeError(isolate, "options must be an object");
                return;
            }
            options = args[optionsArgIndex].As<v8::Object>();
            haveOptions = true;
        }
    }

    bool havePositionalBody = false;
    if (bodyArgIndex >= 0 && args.Length() > bodyArgIndex && !args[bodyArgIndex]->IsNullOrUndefined()) {
        if (!ExtractBody(isolate, args[bodyArgIndex], request.body)) {
            return;
        }
        havePositionalBody = true;
    }

    if (haveOptions && !ApplyOptions(isolate, context, options, request, urlInOptions, !havePositionalBody, binary)) {
        return;
    }

    HttpResponse response;
    std::string error = PerformHttpRequest(request, response);
    if (!error.empty()) {
        v8_error::ThrowError(isolate, "HTTP request failed: " + error);
        return;
    }

    args.GetReturnValue().Set(BuildResponseObject(isolate, context, response, binary));
}

}  // namespace

void JSHttpClient::ConfigureTemplate(v8::Isolate* isolate, v8::Local<v8::FunctionTemplate> tpl) {
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
        isolate, tpl, "request", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
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
        isolate, tpl, "get", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
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
        isolate, tpl, "head", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
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
        isolate, tpl, "delete", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
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
        isolate, tpl, "post", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
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
        isolate, tpl, "put", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
            RequestImpl(args, "PUT", /*urlInOptions=*/false, /*bodyArgIndex=*/1, /*optionsArgIndex=*/2);
        });
}

}  // namespace d2bs::api::classes
