import SwiftUI
import TunnelCore

// MARK: - 日志

public enum LogLevel: String, CaseIterable, Identifiable {
    case info, ok, warn, err
    public var id: String { rawValue }
    var label: String {
        switch self {
        case .info: return "信息"
        case .ok: return "成功"
        case .warn: return "警告"
        case .err: return "错误"
        }
    }
}

public struct LogEntry: Identifiable {
    public let id = UUID()
    public let at: Date
    public let level: LogLevel
    public let source: String
    public let message: String
}

// MARK: - 运行时统计

public struct TunnelRuntime {
    var count: Int = 0
    var bytes: Int64 = 0
    var errCount: Int = 0
    var lastStatus: Int?
    var lastAt: Date?
    var latencyMs: Int?
    var recent: [RequestEvent] = []
}

public struct TrafficBucket: Identifiable {
    public let id: Date
    public let at: Date
    public var count: Int
}

private enum Keys {
    static let server = "tunnelapp.serverURL"
    static let token = "tunnelapp.token"
    static let client = "tunnelapp.clientId"
    static let base = "tunnelapp.baseDomain"
    static let tunnels = "tunnelapp.tunnels"
    static let disabled = "tunnelapp.disabled"
    static let auto = "tunnelapp.autoReconnect"
}

@MainActor
public final class TunnelStore: ObservableObject {
    // 连接配置
    @Published public var serverURL = "wss://frp.xxx.com/tunnel"
    @Published public var token = ""
    @Published public var clientId = "mac-alice"
    @Published public var baseDomain = "frp.xxx.com"
    @Published public var autoReconnect = true

    // 隧道
    @Published public var tunnels: [TunnelConfig] = [
        TunnelConfig(tunnelId: "web1", subdomain: "app1", localAddr: "127.0.0.1:8080")
    ]
    @Published public var disabled: Set<String> = []
    @Published public var runtime: [String: TunnelRuntime] = [:]
    @Published public var localCheck: [String: String] = [:] // tunnelId -> "200 · 12ms" / 错误

    // 运行状态
    @Published public var phase: ConnState = .disconnected
    @Published public var running = false
    @Published public var connectedAt: Date?
    @Published public var now = Date()
    @Published public var lastNote: String = "未连接"

    // 日志与事件
    @Published public var logs: [LogEntry] = []
    @Published public var events: [RequestEvent] = []

    private var client: TunnelClient?
    private var runTask: Task<Void, Never>?
    private var timer: Timer?

    public init() {
        load()
        appendLog(level: .info, source: "app", "工作区已载入，共 \(tunnels.count) 条隧道")
        timer = Timer.scheduledTimer(withTimeInterval: 1, repeats: true) { [weak self] _ in
            Task { @MainActor in self?.now = Date() }
        }
    }

    // MARK: - 日志

    public func appendLog(level: LogLevel = .info, source: String = "app", _ msg: String) {
        logs.append(LogEntry(at: Date(), level: level, source: source, message: msg))
        if logs.count > 2000 { logs.removeFirst(logs.count - 2000) }
    }

    public func clearLogs() { logs.removeAll() }

    public func logText() -> String {
        let f = DateFormatter()
        f.dateFormat = "HH:mm:ss"
        return logs.map { "[\(f.string(from: $0.at))][\($0.level.rawValue)][\($0.source)] \($0.message)" }.joined(separator: "\n")
    }

    // MARK: - 持久化

    public func save() {
        let d = UserDefaults.standard
        d.set(serverURL, forKey: Keys.server)
        d.set(token, forKey: Keys.token)
        d.set(clientId, forKey: Keys.client)
        d.set(baseDomain, forKey: Keys.base)
        d.set(Array(disabled), forKey: Keys.disabled)
        d.set(autoReconnect, forKey: Keys.auto)
        if let data = try? JSONEncoder().encode(tunnels) {
            d.set(data, forKey: Keys.tunnels)
        }
    }

    private func load() {
        let d = UserDefaults.standard
        if let v = d.string(forKey: Keys.server) { serverURL = v }
        if let v = d.string(forKey: Keys.token) { token = v }
        if let v = d.string(forKey: Keys.client) { clientId = v }
        if let v = d.string(forKey: Keys.base) { baseDomain = v }
        if let arr = d.array(forKey: Keys.disabled) as? [String] { disabled = Set(arr) }
        if d.object(forKey: Keys.auto) != nil { autoReconnect = d.bool(forKey: Keys.auto) }
        if let data = d.data(forKey: Keys.tunnels),
           let arr = try? JSONDecoder().decode([TunnelConfig].self, from: data),
           !arr.isEmpty {
            tunnels = arr
        }
    }

    // MARK: - 隧道增删改

    public func isEnabled(_ id: String) -> Bool { !disabled.contains(id) }

    public func toggleEnabled(_ id: String) {
        if disabled.contains(id) { disabled.remove(id) } else { disabled.insert(id) }
        save()
    }

    public func addTunnel(_ t: TunnelConfig) {
        tunnels.append(t)
        save()
        appendLog(level: .info, source: t.tunnelId, "已添加隧道（重连后生效）")
    }

    public func updateTunnel(_ t: TunnelConfig, originalId: String) {
        if let i = tunnels.firstIndex(where: { $0.tunnelId == originalId }) {
            tunnels[i] = t
            if originalId != t.tunnelId {
                if disabled.contains(originalId) {
                    disabled.remove(originalId)
                    disabled.insert(t.tunnelId)
                }
                runtime[t.tunnelId] = runtime.removeValue(forKey: originalId)
            }
            save()
            appendLog(level: .info, source: t.tunnelId, "已更新隧道（重连后生效）")
        }
    }

    public func removeTunnel(_ id: String) {
        tunnels.removeAll { $0.tunnelId == id }
        disabled.remove(id)
        runtime.removeValue(forKey: id)
        save()
        appendLog(level: .warn, source: id, "已删除隧道（重连后生效）")
    }

    public func duplicateTunnel(_ id: String) {
        guard let t = tunnels.first(where: { $0.tunnelId == id }) else { return }
        var n = 2
        var newId = "\(id)-copy"
        while tunnels.contains(where: { $0.tunnelId == newId }) {
            n += 1
            newId = "\(id)-copy\(n)"
        }
        tunnels.append(TunnelConfig(tunnelId: newId, subdomain: nil, pathPrefix: t.pathPrefix, localAddr: t.localAddr))
        save()
    }

    public var enabledTunnels: [TunnelConfig] {
        tunnels.filter { isEnabled($0.tunnelId) }
    }

    // MARK: - 公网地址

    public func publicURL(for t: TunnelConfig) -> String {
        let base = baseDomain.trimmingCharacters(in: .whitespacesAndNewlines)
        if let sub = t.subdomain, !sub.isEmpty {
            return "https://\(sub).\(base)"
        }
        if let p = t.pathPrefix, !p.isEmpty {
            return "https://\(base)\(p)"
        }
        return "https://\(base)"
    }

    public func routeText(for t: TunnelConfig) -> String {
        if let sub = t.subdomain, !sub.isEmpty { return "\(sub).\(baseDomain)" }
        if let p = t.pathPrefix, !p.isEmpty { return "\(baseDomain)\(p)" }
        return baseDomain
    }

    // MARK: - 本地连通测试

    public func testLocal(_ t: TunnelConfig) {
        localCheck[t.tunnelId] = "测试中…"
        Task {
            let t0 = Date()
            guard let url = URL(string: "http://\(t.localAddr)/") else {
                self.localCheck[t.tunnelId] = "地址非法"
                return
            }
            do {
                var req = URLRequest(url: url, timeoutInterval: 5)
                req.httpMethod = "GET"
                let (_, resp) = try await URLSession.shared.data(for: req)
                let ms = Int(Date().timeIntervalSince(t0) * 1000)
                let code = (resp as? HTTPURLResponse)?.statusCode ?? -1
                self.localCheck[t.tunnelId] = "\(code) · \(ms)ms"
                self.appendLog(level: code < 500 ? .ok : .warn, source: t.tunnelId, "本地连通 \(code)（\(ms)ms）")
            } catch {
                self.localCheck[t.tunnelId] = "不通"
                self.appendLog(level: .err, source: t.tunnelId, "本地不通：\(error.localizedDescription)")
            }
        }
    }

    public func testAllLocals() {
        for t in tunnels { testLocal(t) }
    }

    // MARK: - 连接

    public func connect() {
        guard !running else { return }
        guard let url = URL(string: serverURL.trimmingCharacters(in: .whitespaces)) else {
            appendLog(level: .err, source: "app", "服务端地址非法")
            return
        }
        let act = enabledTunnels
        guard !act.isEmpty else {
            appendLog(level: .warn, source: "app", "没有启用的隧道，先启用至少一条")
            return
        }
        save()
        let c = TunnelClient(serverURL: url, token: token, clientId: clientId, tunnels: act)
        c.onLog = { [weak self] s in
            Task { @MainActor in
                guard let self else { return }
                if s.contains("注册成功") {
                    self.appendLog(level: .ok, source: "net", s)
                } else if s.contains("失败") || s.contains("断开") {
                    self.appendLog(level: .warn, source: "net", s)
                } else {
                    self.appendLog(level: .info, source: "net", s)
                }
            }
        }
        c.onState = { [weak self] st in
            Task { @MainActor in
                guard let self else { return }
                self.phase = st
                switch st {
                case .connecting:
                    self.lastNote = "正在握手…"
                case .connected:
                    if self.connectedAt == nil { self.connectedAt = Date() }
                    self.lastNote = "已注册 \(act.count) 条隧道"
                    self.appendLog(level: .ok, source: "net", "控制通道已建立")
                case .disconnected:
                    self.lastNote = "连接断开"
                    if !self.autoReconnect {
                        self.appendLog(level: .warn, source: "net", "自动重连已关闭，停止")
                        self.disconnect()
                    }
                }
            }
        }
        c.onRequest = { [weak self] ev in
            Task { @MainActor in
                guard let self else { return }
                self.events.append(ev)
                if self.events.count > 3000 { self.events.removeFirst(self.events.count - 3000) }
                var r = self.runtime[ev.tunnelId] ?? TunnelRuntime()
                r.count += 1
                r.bytes += Int64(ev.bytes)
                if ev.status >= 500 { r.errCount += 1 }
                r.lastStatus = ev.status
                r.lastAt = ev.at
                r.latencyMs = ev.latencyMs
                r.recent.append(ev)
                if r.recent.count > 60 { r.recent.removeFirst(r.recent.count - 60) }
                self.runtime[ev.tunnelId] = r
            }
        }
        self.client = c
        running = true
        connectedAt = nil
        appendLog(level: .info, source: "net", "正在连接 \(url.host ?? url.absoluteString)，\(act.count) 条隧道…")
        runTask = Task {
            await c.runForever()
            await MainActor.run { [weak self] in
                guard let self else { return }
                self.running = false
                self.phase = .disconnected
                self.appendLog(level: .info, source: "net", "已断开")
            }
        }
    }

    public func disconnect() {
        runTask?.cancel()
        client?.stop()
        client = nil
        running = false
        phase = .disconnected
        connectedAt = nil
        lastNote = "未连接"
        appendLog(level: .info, source: "net", "已手动断开")
    }

    // MARK: - 统计

    public var uptimeText: String {
        guard running, let t0 = connectedAt else { return "—" }
        let s = Int(now.timeIntervalSince(t0))
        if s < 60 { return "\(s)s" }
        if s < 3600 { return "\(s / 60)分\(s % 60)秒" }
        return "\(s / 3600)时\((s % 3600) / 60)分"
    }

    public var totalRequests: Int { events.count }

    public var totalBytes: Int64 { events.reduce(0) { $0 + Int64($1.bytes) } }

    public var okRate: Double {
        guard !events.isEmpty else { return 1 }
        let ok = events.filter { $0.status < 500 }.count
        return Double(ok) / Double(events.count)
    }

    public var avgLatencyMs: Int {
        guard !events.isEmpty else { return 0 }
        return events.suffix(200).map(\.latencyMs).reduce(0, +) / min(events.count, 200)
    }

    public func bucketsLast30Min() -> [TrafficBucket] {
        let cal = Calendar.current
        let nowMin = cal.date(from: cal.dateComponents([.year, .month, .day, .hour, .minute], from: now)) ?? now
        var buckets: [TrafficBucket] = (0..<30).map { i in
            let at = cal.date(byAdding: .minute, value: -(29 - i), to: nowMin) ?? nowMin
            return TrafficBucket(id: at, at: at, count: 0)
        }
        for ev in events where ev.at >= buckets.first!.at {
            let m = cal.date(from: cal.dateComponents([.year, .month, .day, .hour, .minute], from: ev.at)) ?? ev.at
            if let i = buckets.firstIndex(where: { cal.isDate($0.at, equalTo: m, toGranularity: .minute) }) {
                buckets[i].count += 1
            }
        }
        return buckets
    }

    public func perTunnelCounts() -> [(id: String, count: Int)] {
        var m: [String: Int] = [:]
        for ev in events { m[ev.tunnelId, default: 0] += 1 }
        return m.sorted { $0.value > $1.value }.map { ($0.key, $0.value) }
    }
}
