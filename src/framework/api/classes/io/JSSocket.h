#pragma once

#include <v8.h>
#include <cstdint>
#include <string>
#include "api/core/V8Class.h"
#include "api/core/V8Convert.h"
#include "api/core/V8Error.h"

namespace d2bs::api::classes {

// Internal data structure for Socket
// Note: SOCKET is a Windows handle type (UINT_PTR)
struct SocketData {
    uintptr_t handle = ~static_cast<uintptr_t>(0);  // INVALID_SOCKET equivalent without winsock2.h
    bool isConnected = false;
    bool isWsaInitialized = false;

    ~SocketData();  // Defined in JSSocket.cpp (needs winsock2.h for closesocket/WSACleanup)
};

// Socket class - provides TCP socket functionality
// Properties: readable, writeable
// Instance methods: read, send, close
// Static methods: open
class JSSocket : public V8ClassBase<JSSocket, SocketData> {
   public:
    static constexpr std::string_view ClassName = "Socket";

    // Socket objects are obtained via Socket.open() static method, not direct construction
    V8_CLASS_NOT_CONSTRUCTABLE

    static void ConfigureTemplate(v8::Isolate* isolate, v8::Local<v8::FunctionTemplate> tpl);
};

}  // namespace d2bs::api::classes
