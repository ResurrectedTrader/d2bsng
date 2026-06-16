#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>

namespace ix {
class WebSocket;
}  // namespace ix

namespace d2bs::framework::inspector {

class DualModeServer;
class InspectorTarget;

// Process-wide HTTP + WebSocket server fronting the V8 inspector. A single
// server on one localhost port both serves the Chrome DevTools /json discovery
// endpoints and upgrades /<targetId> requests to WebSocket, routing CDP traffic
// to the matching script's InspectorTarget. Started from ScriptEngine when
// [settings]/InspectorPort is positive (its sign encodes enabled/disabled);
// scripts always register a target regardless.
class InspectorServer {
   public:
    static InspectorServer& Instance();

    // Idempotent. Binds 127.0.0.1:<port> on the first call; returns false if the
    // socket could not be bound. Later calls are no-ops returning true.
    bool Start(uint16_t port);
    void Stop();

    void AddTarget(const std::shared_ptr<InspectorTarget>& target);
    void RemoveTarget(const std::string& id);

    // Send a CDP message to the DevTools client attached to `id` (if any).
    // Thread-safe; called from script isolate threads via the inspector channel.
    void Send(const std::string& id, const std::string& message);

    InspectorServer(const InspectorServer&) = delete;
    InspectorServer& operator=(const InspectorServer&) = delete;

   private:
    InspectorServer() = default;
    ~InspectorServer();

    // WebSocket lifecycle, invoked from connection threads with already-extracted
    // values so ix types stay in the .cpp.
    void OnClientConnected(const std::string& connId, const std::string& path, ix::WebSocket& ws);
    void OnClientMessage(const std::string& connId, const std::string& message);
    void OnClientClosed(const std::string& connId);

    [[nodiscard]] std::string BuildListJson() const;
    [[nodiscard]] static std::string BuildVersionJson();

    mutable std::mutex mutex_;
    std::unique_ptr<DualModeServer> server_;
    uint16_t port_ = 0;

    std::map<std::string, std::shared_ptr<InspectorTarget>> targets_;  // id -> target
    std::map<std::string, std::string> connToTarget_;                  // connId -> target id
    // target id -> active ws. The raw pointer aliases ixwebsocket's per-connection
    // shared_ptr<WebSocket>, which WebSocketServer keeps alive in its _clients set
    // for the whole connection. We insert it from the Open callback and erase it
    // from the Close callback, both under mutex_, and ixwebsocket delivers Close on
    // the connection thread before it drops that shared_ptr. So every use of the
    // pointer (Send / RemoveTarget) MUST hold mutex_: that serializes against the
    // Close-callback erase and guarantees the socket is still alive when we touch
    // it. Touching it outside the lock would risk a use-after-free.
    std::map<std::string, ix::WebSocket*> targetToConn_;
};

}  // namespace d2bs::framework::inspector
