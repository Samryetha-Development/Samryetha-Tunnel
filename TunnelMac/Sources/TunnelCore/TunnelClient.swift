import Foundation

/// 单次访客请求事件（供 UI 做统计/图表）
public struct RequestEvent: Sendable {
    public var tunnelId: String
    public var status: Int
    public var bytes: Int
    public var latencyMs: Int
    public var at: Date
    public init(tunnelId: String, status: Int, bytes: Int, latencyMs: Int, at: Date = Date()) {
        self.tunnelId = tunnelId
        self.status = status
        self.bytes = bytes
        self.latencyMs = latencyMs
        self.at = at
    }
}

public enum ConnState: String, Sendable {
    case connecting, connected, disconnected
}

/// 控制通道客户端：一条 WSS 长连接，多隧道复用，断线指数退避重连
public final class TunnelClient {
    public var onLog: ((String) -> Void)?
    public var onRequest: ((RequestEvent) -> Void)?
    public var onState: ((ConnState) -> Void)?

    private let serverURL: URL
    private let token: String
    private let clientId: String
    private let tunnels: [TunnelConfig]
    private var localMap: [String: String] = [:]
    private var task: URLSessionWebSocketTask?
    private var running = false

    public init(serverURL: URL, token: String, clientId: String, tunnels: [TunnelConfig]) {
        self.serverURL = serverURL
        self.token = token
        self.clientId = clientId
        self.tunnels = tunnels
        for t in tunnels { localMap[t.tunnelId] = t.localAddr }
    }

    public func stop() {
        running = false
        task?.cancel(with: .goingAway, reason: nil)
    }

    private func log(_ s: String) {
        onLog?(s)
    }

    /// 阻塞式主循环（带重连），直到 stop()
    public func runForever() async {
        running = true
        var backoff: UInt64 = 1
        while running {
            onState?(.connecting)
            do {
                try await connectOnce()
                backoff = 1  // 成功跑过一次就重置
            } catch {
                log("连接断开: \(error.localizedDescription)，\(backoff)s 后重连")
            }
            onState?(.disconnected)
            if !running { break }
            try? await Task.sleep(nanoseconds: backoff * 1_000_000_000)
            backoff = min(backoff * 2, 30)
        }
    }

    private func connectOnce() async throws {
        var req = URLRequest(url: serverURL, timeoutInterval: 15)
        req.setValue("Bearer \(token)", forHTTPHeaderField: "Authorization")
        let session = URLSession(configuration: .default)
        let ws = session.webSocketTask(with: req)
        self.task = ws
        ws.resume()
        log("已连接 \(serverURL.absoluteString)，正在注册 \(tunnels.count) 条隧道…")

        // 注册
        let reg = makeRegisterJSON(clientId: clientId, tunnels: tunnels)
        try await ws.send(.string(reg))
        onState?(.connected)

        // 接收循环
        while running {
            let msg = try await ws.receive()
            let text: String
            switch msg {
            case .string(let s): text = s
            case .data(let d): text = String(data: d, encoding: .utf8) ?? ""
            @unknown default: continue
            }
            guard let parsed = try? parseServerMsg(text) else {
                log("忽略非法帧: \(text.prefix(120))")
                continue
            }
            switch parsed {
            case .ping:
                try? await ws.send(.string(makePongJSON()))
            case .registerAck(let ok, let err):
                if ok {
                    log("注册成功 client_id=\(clientId)")
                } else {
                    log("注册失败: \(err ?? "unknown")")
                }
            case .openStream(let sid, let tid, let method, let path, let headers, let body):
                // 每个访客请求起一个 Task 去连内网，不阻塞接收循环
                Task { await self.serveStream(ws: ws, streamId: sid, tunnelId: tid, method: method, path: path, headers: headers, body: body) }
            }
        }
    }

    private func serveStream(ws: URLSessionWebSocketTask, streamId: UInt64, tunnelId: String, method: String, path: String, headers: [String: String], body: Data) async {
        let t0 = Date()
        func emit(status: Int, bytes: Int) {
            let ms = Int(Date().timeIntervalSince(t0) * 1000)
            onRequest?(RequestEvent(tunnelId: tunnelId, status: status, bytes: bytes, latencyMs: ms))
        }
        guard let local = localMap[tunnelId] else {
            let r = makeResponseJSON(streamId: streamId, status: 502, headers: [:], body: Data("unknown tunnel".utf8))
            try? await ws.send(.string(r))
            emit(status: 502, bytes: 0)
            return
        }
        // 内网目标：http://localAddr + path
        guard let url = URL(string: "http://\(local)\(path)") else {
            let r = makeResponseJSON(streamId: streamId, status: 502, headers: [:], body: Data("bad local url".utf8))
            try? await ws.send(.string(r))
            emit(status: 502, bytes: 0)
            return
        }
        var req = URLRequest(url: url, timeoutInterval: 25)
        req.httpMethod = method
        // 透传请求头（去掉 host/content-length，由 URLSession 自己算）
        for (k, v) in headers {
            let lk = k.lowercased()
            if lk == "host" || lk == "content-length" { continue }
            req.setValue(v, forHTTPHeaderField: k)
        }
        if !body.isEmpty { req.httpBody = body }

        do {
            let (data, resp) = try await URLSession.shared.data(for: req)
            let status = (resp as? HTTPURLResponse)?.statusCode ?? 200
            var outHeaders: [String: String] = [:]
            if let http = resp as? HTTPURLResponse {
                for (k, v) in http.allHeaderFields {
                    outHeaders["\(k)"] = "\(v)"
                }
            }
            // 回包限 8MB（和服务端对齐）
            let capped = data.count > 8 * 1024 * 1024 ? data.prefix(8 * 1024 * 1024) : data[...]
            let r = makeResponseJSON(streamId: streamId, status: status, headers: outHeaders, body: Data(capped))
            try await ws.send(.string(r))
            emit(status: status, bytes: data.count)
        } catch {
            let msg = "local fetch failed: \(error.localizedDescription)"
            let r = makeResponseJSON(streamId: streamId, status: 502, headers: [:], body: Data(msg.utf8))
            try? await ws.send(.string(r))
            emit(status: 502, bytes: 0)
        }
    }
}
