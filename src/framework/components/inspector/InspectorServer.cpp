#include "components/inspector/InspectorServer.h"

#include <ixwebsocket/IXConnectionState.h>
#include <ixwebsocket/IXHttp.h>
#include <ixwebsocket/IXNetSystem.h>
#include <ixwebsocket/IXSocket.h>
#include <ixwebsocket/IXWebSocket.h>
#include <ixwebsocket/IXWebSocketMessage.h>
#include <ixwebsocket/IXWebSocketServer.h>
#include <nlohmann/json.hpp>

#include <format>
#include <functional>
#include <utility>
#include <vector>

#include "components/inspector/InspectorTarget.h"
#include "utils/utils.h"

namespace d2bs::framework::inspector {

namespace {

const std::shared_ptr<spdlog::logger>& Logger() {
    static std::shared_ptr<spdlog::logger> logger = d2bs::utils::GetLogger("inspector");
    return logger;
}

// ixwebsocket's listener binds with SO_REUSEADDR, which on Windows permits a
// second ACTIVE bind on the same port (unlike Unix), silently splitting incoming
// connections between processes - a second multi-boxed game instance would
// "successfully" start its inspector on the same port and break both. Probe the
// port with SO_EXCLUSIVEADDRUSE first so an in-use port fails loudly instead.
// The probe socket is closed before the real bind; the gap is harmless (worst
// case is the pre-probe behavior).
bool IsPortFree(uint16_t port) {
    SOCKET probe = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (probe == INVALID_SOCKET) {
        return false;
    }
    BOOL exclusive = TRUE;
    ::setsockopt(probe, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char*>(&exclusive), sizeof(exclusive));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(port);
    const bool isFree = ::bind(probe, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) != SOCKET_ERROR;
    ::closesocket(probe);
    return isFree;
}

}  // namespace

// One localhost port serving both the /json discovery endpoints (plain HTTP)
// and the /<targetId> WebSocket upgrades. ix::HttpServer does exactly this, but
// its dispatch compares the Upgrade header value case-SENSITIVELY ("websocket"),
// and the browser-side proxy chrome://inspect attaches through sends
// "Upgrade: WebSocket" - so every click-inspect upgrade fell through to the HTTP
// handler and got a 404. The header value is case-insensitive per RFC 6455/7230
// (ixwebsocket's own WS handshake checks it case-insensitively; only the
// dispatch is strict), and ix::HttpServer is final, so this reimplements its
// small dispatch on ix::WebSocketServer with a case-insensitive check.
class DualModeServer : public ix::WebSocketServer {
   public:
    using HttpHandler = std::function<ix::HttpResponsePtr(const ix::HttpRequestPtr&)>;

    DualModeServer(int port, const std::string& host) : ix::WebSocketServer(port, host) {}

    void SetHttpHandler(HttpHandler handler) { httpHandler_ = std::move(handler); }

   private:
    // Matches ix::HttpServer::kDefaultTimeoutSecs.
    static constexpr int PARSE_TIMEOUT_SECS = 30;

    void handleConnection(std::unique_ptr<ix::Socket> socket,
                          std::shared_ptr<ix::ConnectionState> connectionState) override {
        auto ret = ix::Http::parseRequest(socket, PARSE_TIMEOUT_SECS);
        if (std::get<0>(ret)) {
            const auto& request = std::get<2>(ret);
            // Header NAMES are case-insensitive in the map; the VALUE compare
            // must be case-insensitive too.
            if (utils::ToLower(request->headers["Upgrade"]).find("websocket") != std::string::npos) {
                handleUpgrade(std::move(socket), connectionState, request);
            } else if (httpHandler_) {
                ix::Http::sendResponse(httpHandler_(request), socket);
            }
        }
        connectionState->setTerminated();
    }

    HttpHandler httpHandler_;
};

InspectorServer& InspectorServer::Instance() {
    static InspectorServer instance;
    return instance;
}

// NOLINTNEXTLINE(bugprone-exception-escape) - Stop() may throw (mutex/alloc); terminate is fine at static teardown
InspectorServer::~InspectorServer() {
    Stop();
}

bool InspectorServer::Start(uint16_t port) {
    std::scoped_lock lock(mutex_);
    if (server_) {
        return true;
    }

    // WinSock is already up (we run inside the game), but ixwebsocket tracks its
    // own init refcount; balance this with uninitNetSystem() in Stop().
    ix::initNetSystem();

    if (!IsPortFree(port)) {
        Logger()->error("port {} is already in use (another instance?)", port);
        ix::uninitNetSystem();
        return false;
    }

    auto server = std::make_unique<DualModeServer>(static_cast<int>(port), "127.0.0.1");

    // Plain HTTP: serve the Chrome DevTools discovery endpoints. WebSocket
    // upgrades never reach this handler - DualModeServer routes them to the
    // client-message callback below.
    server->SetHttpHandler([this](const ix::HttpRequestPtr& request) -> ix::HttpResponsePtr {
        ix::WebSocketHttpHeaders headers;
        headers["Content-Type"] = "application/json; charset=UTF-8";
        headers["Cache-Control"] = "no-cache";
        // ixwebsocket serves one request per connection and then closes the
        // socket, but answers as HTTP/1.1 (implicit keep-alive) - say close
        // explicitly so chrome://inspect's poller doesn't try to reuse a dead
        // connection on its next poll.
        headers["Connection"] = "close";
        // Match on the path only - ixwebsocket leaves any query string on the uri.
        std::string path = request->uri;
        if (const auto query = path.find('?'); query != std::string::npos) {
            path.erase(query);
        }
        if (path == "/json/version") {
            return std::make_shared<ix::HttpResponse>(200, "OK", ix::HttpErrorCode::Ok, headers, BuildVersionJson());
        }
        if (path == "/json" || path == "/json/list") {
            return std::make_shared<ix::HttpResponse>(200, "OK", ix::HttpErrorCode::Ok, headers, BuildListJson());
        }
        // /json/protocol (the CDP schema) is intentionally not served - DevTools
        // ships its own; only programmatic CDP clients fetch it from the target.
        ix::WebSocketHttpHeaders notFoundHeaders;
        notFoundHeaders["Connection"] = "close";
        return std::make_shared<ix::HttpResponse>(404, "Not Found", ix::HttpErrorCode::Ok, notFoundHeaders,
                                                  std::string("Not Found"));
    });

    server->setOnClientMessageCallback([this](const std::shared_ptr<ix::ConnectionState>& state, ix::WebSocket& ws,
                                              const ix::WebSocketMessagePtr& msg) {
        switch (msg->type) {
            case ix::WebSocketMessageType::Open:
                OnClientConnected(state->getId(), msg->openInfo.uri, ws);
                break;
            case ix::WebSocketMessageType::Message:
                OnClientMessage(state->getId(), msg->str);
                break;
            case ix::WebSocketMessageType::Close:
                OnClientClosed(state->getId());
                break;
            default:
                break;
        }
    });

    if (const auto [ok, error] = server->listen(); !ok) {
        Logger()->error("listen on 127.0.0.1:{} failed: {}", port, error);
        ix::uninitNetSystem();  // balance the initNetSystem() above when the bind fails
        return false;
    }
    server->start();
    server_ = std::move(server);
    port_ = port;
    Logger()->info("listening on http://127.0.0.1:{}", port);
    return true;
}

void InspectorServer::Stop() {
    std::unique_ptr<DualModeServer> server;
    std::vector<std::shared_ptr<InspectorTarget>> targets;
    {
        std::scoped_lock lock(mutex_);
        server = std::move(server_);
        for (const auto& entry : targets_) {
            targets.push_back(entry.second);
        }
        connToTarget_.clear();
        targetToConn_.clear();
        // targets_ is NOT cleared: scripts keep their ScriptInspectors (and thus
        // their registered targets) across a server stop, so a later Start - a
        // port change or re-enable - re-exposes every live script with no
        // re-registration. RemoveTarget, on ScriptInspector teardown, is what
        // drops a target.
        port_ = 0;
    }
    // Wake any script paused at a breakpoint: with the transport going away it
    // would otherwise block forever in its pause loop. A Disconnect resumes it
    // and tears its session down; the target stays registered.
    for (const auto& target : targets) {
        target->Push(InspectorTarget::EventKind::Disconnected);
    }
    // Stop outside the lock: the server's stop() joins connection threads, which
    // may be mid-callback taking mutex_. With the conn maps cleared those
    // callbacks are no-ops, so they cannot deadlock against this join.
    if (server) {
        server->stop();
        ix::uninitNetSystem();
    }
}

void InspectorServer::AddTarget(const std::shared_ptr<InspectorTarget>& target) {
    std::scoped_lock lock(mutex_);
    targets_[target->Id()] = target;
}

void InspectorServer::RemoveTarget(const std::string& id) {
    std::scoped_lock lock(mutex_);
    targets_.erase(id);
    // Close any live DevTools connection so the client sees the target go away.
    // The conn maps are cleaned by the resulting Close callback (which finds no
    // target and no-ops). close() only flags the socket; the connection thread
    // does the actual teardown, so calling it under the lock is safe and quick.
    if (auto it = targetToConn_.find(id); it != targetToConn_.end()) {
        it->second->close();
    }
}

void InspectorServer::Send(const std::string& id, const std::string& message) {
    // sendText runs under mutex_ deliberately - see targetToConn_ for the socket
    // lifetime invariant. ix::WebSocket::sendText is thread-safe and only buffers
    // the message (it does not block on socket I/O), so holding the lock across it
    // does not stall the isolate thread on the network.
    std::scoped_lock lock(mutex_);
    if (auto it = targetToConn_.find(id); it != targetToConn_.end()) {
        it->second->sendText(message);
    }
}

void InspectorServer::OnClientConnected(const std::string& connId, const std::string& path, ix::WebSocket& ws) {
    // path is like "/12345" (the script's thread id); strip the leading slash.
    std::string id = path;
    if (!id.empty() && id.front() == '/') {
        id.erase(0, 1);
    }

    std::shared_ptr<InspectorTarget> target;
    {
        std::scoped_lock lock(mutex_);
        auto it = targets_.find(id);
        if (it == targets_.end()) {
            // Unknown target - e.g. a DevTools tab holding a target id from
            // before a restart (ids are per-run thread ids).
            Logger()->warn("ws upgrade for unknown target '{}' - closing", id);
            ws.close();
            return;
        }
        if (targetToConn_.contains(id)) {
            Logger()->warn("ws upgrade for '{}' ({}) rejected - a DevTools client is already attached", id,
                           it->second->Title());
            ws.close();
            return;
        }
        target = it->second;
        connToTarget_[connId] = id;
        targetToConn_[id] = &ws;
        Logger()->info("DevTools attached to '{}' ({})", id, target->Title());
    }
    target->Push(InspectorTarget::EventKind::Connected);
}

void InspectorServer::OnClientMessage(const std::string& connId, const std::string& message) {
    std::shared_ptr<InspectorTarget> target;
    {
        std::scoped_lock lock(mutex_);
        auto it = connToTarget_.find(connId);
        if (it == connToTarget_.end()) {
            return;
        }
        if (auto tit = targets_.find(it->second); tit != targets_.end()) {
            target = tit->second;
        }
    }
    if (target) {
        target->Push(InspectorTarget::EventKind::Message, message);
    }
}

void InspectorServer::OnClientClosed(const std::string& connId) {
    std::shared_ptr<InspectorTarget> target;
    {
        std::scoped_lock lock(mutex_);
        auto it = connToTarget_.find(connId);
        if (it == connToTarget_.end()) {
            return;
        }
        const std::string id = it->second;
        connToTarget_.erase(it);
        targetToConn_.erase(id);
        Logger()->info("DevTools detached from '{}'", id);
        if (auto tit = targets_.find(id); tit != targets_.end()) {
            target = tit->second;
        }
    }
    if (target) {
        target->Push(InspectorTarget::EventKind::Disconnected);
    }
}

std::string InspectorServer::BuildVersionJson() {
    // Protocol-Version 1.3 is what current DevTools negotiates over CDP.
    const nlohmann::json version{{"Browser", "d2bsng/v8"}, {"Protocol-Version", "1.3"}};
    return version.dump();
}

std::string InspectorServer::BuildListJson() const {
    std::scoped_lock lock(mutex_);
    nlohmann::json list = nlohmann::json::array();
    for (const auto& [id, target] : targets_) {
        const std::string wsUrl = std::format("127.0.0.1:{}/{}", port_, id);
        // inspector.html is the full bundled frontend and is verified to attach to
        // our raw V8 sessions; js_app.html (the Node-specific frontend Node serves
        // by default) is not. Emit the Compat key too for tools that read it.
        const std::string frontend =
            "devtools://devtools/bundled/inspector.html?experiments=true&v8only=true&ws=" + wsUrl;
        // type "page", NOT "node": chrome://inspect's click-inspect opens node
        // targets with the tip-of-tree js_app frontend fetched from
        // chrome-devtools-frontend.appspot.com (see DevToolsWindow::
        // OpenDevToolsWindow, "Direct node targets will always open using ToT
        // front-end") - if that remote fetch stalls the window never attaches.
        // "page" targets open the bundled frontend and attach via the browser
        // proxy, which works against our raw V8 sessions.
        list.push_back({
            {"id", id},
            {"type", "page"},
            {"title", target->Title()},
            {"description", "d2bs script"},
            {"url", target->Url()},
            {"webSocketDebuggerUrl", "ws://" + wsUrl},
            {"devtoolsFrontendUrl", frontend},
            {"devtoolsFrontendUrlCompat", frontend},
        });
    }
    return list.dump();
}

}  // namespace d2bs::framework::inspector
