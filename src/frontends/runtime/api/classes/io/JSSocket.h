#pragma once

#include <cstdint>

#include "api/core/Class.h"

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
class JSSocket : public ClassBase<JSSocket, SocketData> {
   public:
    static constexpr std::string_view ClassName = "Socket";

    // Socket objects are obtained via Socket.open() static method, not direct construction
    static void Configure(const ub::Class<SocketData>& cls);
};

}  // namespace d2bs::api::classes
