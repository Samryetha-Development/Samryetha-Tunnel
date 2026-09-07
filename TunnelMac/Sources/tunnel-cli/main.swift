import Foundation
import TunnelCore

// 用法：
//   tunnel-cli --server wss://frp.xxx.com/tunnel --token SECRET --client-id mac-alice --config tunnels.json
// tunnels.json: {"tunnels":[{"tunnel_id":"web1","subdomain":"app1","local_addr":"127.0.0.1:8080"}]}

@main
struct CLI {
    static func main() async {
        let args = CommandLine.arguments
        func arg(_ name: String) -> String? {
            guard let i = args.firstIndex(of: name), i + 1 < args.count else { return nil }
            return args[i + 1]
        }
        guard let server = arg("--server"),
              let url = URL(string: server),
              let token = arg("--token"),
              let clientId = arg("--client-id") else {
            print("用法: tunnel-cli --server wss://frp.xxx.com/tunnel --token SECRET --client-id mac-alice [--config tunnels.json]")
            return
        }
        var tunnels: [TunnelConfig] = []
        if let cfgPath = arg("--config"),
           let data = try? Data(contentsOf: URL(fileURLWithPath: cfgPath)),
           let obj = try? JSONSerialization.jsonObject(with: data) as? [String: Any],
           let arr = obj["tunnels"] as? [[String: Any]] {
            for t in arr {
                guard let tid = t["tunnel_id"] as? String,
                      let local = t["local_addr"] as? String else { continue }
                tunnels.append(TunnelConfig(
                    tunnelId: tid,
                    subdomain: t["subdomain"] as? String,
                    pathPrefix: t["path_prefix"] as? String,
                    localAddr: local
                ))
            }
        } else {
            // 默认示例：本地 8080 -> app1 子域名（方便第一次跑通）
            tunnels = [TunnelConfig(tunnelId: "web1", subdomain: "app1", localAddr: "127.0.0.1:8080")]
            print("未指定 --config，使用默认单隧道 web1 -> 127.0.0.1:8080")
        }

        let client = TunnelClient(serverURL: url, token: token, clientId: clientId, tunnels: tunnels)
        client.onLog = { print("[tunnel] \($0)") }
        await client.runForever()
    }
}
