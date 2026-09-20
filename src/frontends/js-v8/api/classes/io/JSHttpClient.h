#pragma once

#include <v8.h>
#include <string_view>

#include "api/core/V8Class.h"
#include "api/core/V8Convert.h"
#include "api/core/V8Error.h"

namespace d2bs::api::classes {

// HttpClient exposes only static methods and carries no per-instance state.
struct HttpClientData {};

// HttpClient - a blocking HTTP/HTTPS client backed by WinHTTP.
//
// Every method blocks the calling script until the full response has been
// received, then returns a plain response object:
//
//     { status, statusText, ok, headers, url, body }
//
// where:
//   status      {number}  - HTTP status code (e.g. 200)
//   statusText  {string}  - reason phrase (e.g. "OK")
//   ok          {boolean} - true when status is in the 200-299 range
//   headers     {object}  - response headers, names lowercased
//   url         {string}  - the final URL (after any redirects)
//   body        {string|ArrayBuffer} - a UTF-8 string, or an ArrayBuffer when
//                           the request options set binary: true
//
// HTTPS connections validate against the Windows certificate store, and the
// system (Internet Options) proxy configuration is honored. Non-2xx responses
// are returned normally - inspect status / ok; only transport-level failures
// (DNS, TLS, connection, timeout) throw.
class JSHttpClient : public V8ClassBase<JSHttpClient, HttpClientData> {
   public:
    static constexpr std::string_view ClassName = "HttpClient";

    // HttpClient is a namespace-like holder of static methods, never constructed.
    V8_CLASS_NOT_CONSTRUCTABLE

    static void ConfigureTemplate(v8::Isolate* isolate, v8::Local<v8::FunctionTemplate> tpl);
};

}  // namespace d2bs::api::classes
