import Foundation

/// 单条隧道配置（localAddr 只在客户端用，不发给服务端）
public struct TunnelConfig: Codable, Identifiable, Equatable {
    public var id: String { tunnelId }
    public var tunnelId: String
    public var subdomain: String?
    public var pathPrefix: String?
    public var localAddr: String  // 如 "127.0.0.1:8080"

    public init(tunnelId: String, subdomain: String? = nil, pathPrefix: String? = nil, localAddr: String) {
        self.tunnelId = tunnelId
        self.subdomain = subdomain
        self.pathPrefix = pathPrefix
        self.localAddr = localAddr
    }

    enum CodingKeys: String, CodingKey {
        case tunnelId = "tunnel_id"
        case subdomain
        case pathPrefix = "path_prefix"
        case localAddr = "local_addr"
    }

    /// 发给服务端的注册字典（去掉 localAddr）
    public func registerDict() -> [String: Any] {
        var d: [String: Any] = ["tunnel_id": tunnelId]
        if let s = subdomain, !s.isEmpty { d["subdomain"] = s }
        if let p = pathPrefix, !p.isEmpty { d["path_prefix"] = p }
        return d
    }
}

/// 服务端 -> 客户端消息
public enum ServerMsg {
    case registerAck(ok: Bool, error: String?)
    case ping
    case openStream(streamId: UInt64, tunnelId: String, method: String, path: String, headers: [String: String], body: Data)
}

public enum ProtoError: Error {
    case badJSON
}

/// 解析服务端帧（Text JSON）
public func parseServerMsg(_ text: String) throws -> ServerMsg {
    guard let data = text.data(using: .utf8),
          let obj = try? JSONSerialization.jsonObject(with: data) as? [String: Any],
          let type_ = obj["type"] as? String else {
        throw ProtoError.badJSON
    }
    switch type_ {
    case "ping":
        return .ping
    case "register_ack":
        let ok = obj["ok"] as? Bool ?? false
        let err = obj["error"] as? String
        return .registerAck(ok: ok, error: err)
    case "open_stream":
        guard let sidNum = obj["stream_id"] as? NSNumber,
              let tid = obj["tunnel_id"] as? String,
              let method = obj["method"] as? String,
              let path = obj["path"] as? String else {
            throw ProtoError.badJSON
        }
        let headers = obj["headers"] as? [String: String] ?? [:]
        var body = Data()
        if let b64 = obj["body_b64"] as? String, !b64.isEmpty {
            body = Data(base64Encoded: b64) ?? Data()
        }
        return .openStream(streamId: sidNum.uint64Value, tunnelId: tid, method: method, path: path, headers: headers, body: body)
    default:
        throw ProtoError.badJSON
    }
}

public func makeRegisterJSON(clientId: String, tunnels: [TunnelConfig]) -> String {
    let arr = tunnels.map { $0.registerDict() }
    let obj: [String: Any] = ["type": "register", "client_id": clientId, "tunnels": arr]
    let data = (try? JSONSerialization.data(withJSONObject: obj)) ?? Data()
    return String(data: data, encoding: .utf8) ?? "{}"
}

public func makePongJSON() -> String {
    "{\"type\":\"pong\"}"
}

public func makeResponseJSON(streamId: UInt64, status: Int, headers: [String: String], body: Data) -> String {
    let obj: [String: Any] = [
        "type": "response",
        "stream_id": streamId,
        "status": status,
        "headers": headers,
        "body_b64": body.base64EncodedString(),
    ]
    let data = (try? JSONSerialization.data(withJSONObject: obj)) ?? Data()
    return String(data: data, encoding: .utf8) ?? "{}"
}
