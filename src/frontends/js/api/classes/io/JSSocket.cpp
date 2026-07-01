#include "JSSocket.h"

#include <array>

#ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>

#pragma comment(lib, "Ws2_32.lib")

#include "proxy/ProxyBypass.h"

namespace d2bs::api::classes {

SocketData::~SocketData() {
    if (handle != INVALID_SOCKET) {
        closesocket(handle);
    }
    if (isWsaInitialized) {
        WSACleanup();
    }
}

void JSSocket::ConfigureTemplate(v8::Isolate* isolate, v8::Local<v8::FunctionTemplate> tpl) {
    auto inst = tpl->InstanceTemplate();
    auto proto = tpl->PrototypeTemplate();

    // Properties
    /// @description Whether the socket has data ready to read (1 = ready, 0 = not, -1 = error).
    /// @type {number}
    Property(
        isolate, inst, "readable", +[](v8::Local<v8::Name> property, const v8::PropertyCallbackInfo<v8::Value>& info) {
            auto* isolate = info.GetIsolate();

            auto data = Unwrap(info.Holder());
            if (!data || data->handle == INVALID_SOCKET) {
                info.GetReturnValue().Set(0);
                return;
            }

            // Use select() to check if data is available
            fd_set readSet;
            struct timeval timeout;
            timeout.tv_sec = 0;
            timeout.tv_usec = 100;  // 100 microseconds

            FD_ZERO(&readSet);
            FD_SET(data->handle, &readSet);

            int32_t result = select(1, &readSet, nullptr, nullptr, &timeout);
            info.GetReturnValue().Set(v8_convert::ToV8(isolate, result));
        });

    /// @description Whether the socket is ready to accept writes (1 = ready, 0 = not, -1 = error).
    /// @type {number}
    Property(
        isolate, inst, "writeable", +[](v8::Local<v8::Name> property, const v8::PropertyCallbackInfo<v8::Value>& info) {
            auto* isolate = info.GetIsolate();

            auto data = Unwrap(info.Holder());
            if (!data || data->handle == INVALID_SOCKET) {
                info.GetReturnValue().Set(0);
                return;
            }

            // Use select() to check if socket is writable
            fd_set writeSet;
            struct timeval timeout;
            timeout.tv_sec = 0;
            timeout.tv_usec = 100;  // 100 microseconds

            FD_ZERO(&writeSet);
            FD_SET(data->handle, &writeSet);

            int32_t result = select(1, nullptr, &writeSet, nullptr, &timeout);
            info.GetReturnValue().Set(v8_convert::ToV8(isolate, result));
        });

    // Instance Methods
    /// @description Reads available data from the socket via a single blocking recv() call.
    /// @signature read()
    /// @returns {string} - Bytes received, empty on a clean peer close; throws if not connected or recv() fails.
    /// @throws {Error} - if the socket is not connected, or if recv() fails.
    Method(
        isolate, proto, "read", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
            auto* isolate = args.GetIsolate();

            auto data = Unwrap(args.This());
            if (!data || data->handle == INVALID_SOCKET) {
                v8_error::ThrowError(isolate, "Socket is not connected");
                return;
            }

            std::array<char, 10000> buffer{};
            int32_t bytesRead = recv(data->handle, buffer.data(), buffer.size(), 0);
            if (bytesRead == -1) {
                v8_error::ThrowError(isolate, "Failed to read from socket");
                return;
            }
            std::string result(buffer.data(), bytesRead);
            args.GetReturnValue().Set(v8_convert::ToV8(isolate, result));
        });

    /// @description Sends a string over the socket via a single send() call.
    /// @signature send(msg: string)
    /// @param msg {string} - Data to send.
    /// @returns {number} - Bytes sent (may be less than msg length; -1 on error).
    /// @throws {Error} - if the socket is not connected.
    Method(
        isolate, proto, "send", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
            auto* isolate = args.GetIsolate();

            if (!v8_error::CheckArgCount(args, 1, "send")) {
                return;
            }

            if (!args[0]->IsString()) {
                v8_error::ThrowTypeError(isolate, "send() requires a string argument");
                return;
            }

            auto data = Unwrap(args.This());
            if (!data || data->handle == INVALID_SOCKET) {
                v8_error::ThrowError(isolate, "Socket is not connected");
                return;
            }

            std::string msg = v8_convert::ToString(isolate, args[0]);
            // Return number of bytes sent so caller can detect partial writes. API enhancement.
            args.GetReturnValue().Set(send(data->handle, msg.c_str(), static_cast<int32_t>(msg.length()), 0));
        });

    /// @description Closes the socket and releases its resources; idempotent.
    /// @signature close()
    /// @returns {undefined} - Nothing.
    Method(
        isolate, proto, "close", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
            auto data = Unwrap(args.This());
            if (!data) {
                return;
            }

            if (data->handle != INVALID_SOCKET) {
                closesocket(data->handle);
                data->handle = INVALID_SOCKET;
                data->isConnected = false;
            }

            if (data->isWsaInitialized) {
                WSACleanup();
                data->isWsaInitialized = false;
            }
        });

    // Static Methods
    /// @description Opens a blocking TCP connection to the given host and port.
    /// @signature open(host: string, port: number)
    /// @param host {string} - Hostname or IP address to connect to.
    /// @param port {number} - TCP port to connect to (coerced to int32).
    /// @returns {Socket|boolean} - Connected Socket on success, or false if called with fewer than 2 args.
    /// @throws {Error} - if Winsock init fails, the host cannot be resolved, or the socket cannot be created or
    /// connected.
    StaticMethod(
        isolate, tpl, "open", +[](const v8::FunctionCallbackInfo<v8::Value>& args) {
            auto* isolate = args.GetIsolate();

            if (args.Length() < 2) {
                args.GetReturnValue().SetFalse();
                return;
            }

            if (!args[0]->IsString()) {
                v8_error::ThrowTypeError(isolate, "Socket.open() requires host as first argument");
                return;
            }

            if (!args[1]->IsNumber()) {
                v8_error::ThrowTypeError(isolate, "Socket.open() requires port as second argument");
                return;
            }

            // Note: We are not implementing a host whitelist for this project for now.
            std::string host = v8_convert::ToString(isolate, args[0]);
            int32_t port = v8_convert::ToInt32(isolate, args[1]);

            // Initialize Winsock
            WSADATA wsaData;
            if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
                v8_error::ThrowError(isolate, "Failed to initialize Winsock");
                return;
            }

            // Resolve hostname using getaddrinfo (modern replacement for gethostbyname)
            struct addrinfo hints = {};
            hints.ai_family = AF_INET;
            hints.ai_socktype = SOCK_STREAM;
            hints.ai_protocol = IPPROTO_TCP;

            struct addrinfo* result = nullptr;
            std::string portStr = std::to_string(port);
            int32_t addrResult = getaddrinfo(host.c_str(), portStr.c_str(), &hints, &result);
            if (addrResult != 0) {
                WSACleanup();
                v8_error::ThrowError(isolate, "Cannot resolve host");
                return;
            }

            // Create socket
            SOCKET sock = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
            if (sock == INVALID_SOCKET) {
                freeaddrinfo(result);
                WSACleanup();
                v8_error::ThrowError(isolate, "Failed to create socket");
                return;
            }

            // Connect. Script sockets are not the game's Battle.net traffic, so keep
            // them off the -proxy SOCKS5 tunnel (the detour passes bypassed connects through).
            proxy::BypassScope noProxy;
            if (connect(sock, result->ai_addr, static_cast<int>(result->ai_addrlen)) != 0) {
                closesocket(sock);
                freeaddrinfo(result);
                WSACleanup();
                v8_error::ThrowError(isolate, "Failed to connect");
                return;
            }

            freeaddrinfo(result);

            // Create Socket object with proper initialization and weak reference
            auto context = isolate->GetCurrentContext();
            auto data = std::make_unique<SocketData>();
            data->handle = sock;
            data->isConnected = true;
            data->isWsaInitialized = true;

            auto obj = CreateInstance(isolate, context, std::move(data));
            if (obj.IsEmpty())
                return;
            args.GetReturnValue().Set(obj);
        });
}

}  // namespace d2bs::api::classes
