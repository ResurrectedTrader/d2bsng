#pragma once

#include <v8.h>
#include <string>
#include "api/core/Class.h"
#include "api/core/Convert.h"
#include "api/core/Error.h"

namespace d2bs::runtime::api::classes {

// Internal data structure for Socket. Owns a connected socket and one WSAStartup reference from construction.
// Note: SOCKET is a Windows handle type (UINT_PTR)
struct SocketData {
    uintptr_t handle = ~static_cast<uintptr_t>(0);  // INVALID_SOCKET equivalent without winsock2.h
    bool isConnected = false;
    bool isWsaInitialized = false;

    explicit SocketData(uintptr_t connected) : handle(connected), isConnected(true), isWsaInitialized(true) {}
    SocketData(const SocketData&) = delete;
    SocketData& operator=(const SocketData&) = delete;
    SocketData(SocketData&&) = delete;
    SocketData& operator=(SocketData&&) = delete;
    ~SocketData();  // Defined in JSSocket.cpp (needs winsock2.h for closesocket/WSACleanup)
};

// Socket class - provides TCP socket functionality
// Properties: readable, writeable
// Instance methods: read, send, close
// Static methods: open
class JSSocket : public ClassBase<JSSocket, SocketData> {
   public:
    static constexpr std::string_view ClassName = "Socket";

    // Socket objects are obtained via Socket.open() static method, not direct construction
    V8_CLASS_NOT_CONSTRUCTABLE

    static void ConfigureTemplate(v8::Isolate* isolate, v8::Local<v8::FunctionTemplate> tpl);
};

}  // namespace d2bs::runtime::api::classes
