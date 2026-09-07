# tunnel

Self-hosted ngrok alternative: Rust tunnel server (single-443, Caddy-friendly) + native SwiftUI macOS client with subdomain & subpath routing.

自建内网穿透：Rust 服务端（单 443 端口，Caddy 反代友好）+ 原生 SwiftUI macOS 客户端，支持子域名与子路由分发，多隧道复用一条 WebSocket 长连接。

```
[内网 App 127.0.0.1:8080]
      ↕ 本地 TCP/HTTP
[Swift 客户端] —WSS wss://frp.xxx.com/tunnel→ [Caddy 443] → [Rust 127.0.0.1:18080]
                                                                  ↕
                                    访客：app1.frp.xxx.com / frp.xxx.com/u/alice/api/
```

## 目录

| 目录 | 说明 |
| --- | --- |
| `tunnel-server/` | Rust 服务端（tokio + axum），控制通道 + 访客分发 |
| `TunnelMac/` | Swift 客户端：`TunnelCore` 协议库 + `tunnel-cli` 联调工具 + `TunnelApp` 图形界面 |

## 快速开始

**服务端**（见 `tunnel-server/README.md`）：

```bash
cd tunnel-server
PORT=18080 SERVER_TOKEN=换成强随机串 BASE_DOMAIN=frp.xxx.com cargo run --release
```

Caddy 配置见 `tunnel-server/Caddyfile.example`（主域名 + 泛域名反代到 `127.0.0.1:18080`）。

**客户端 CLI**（见 `TunnelMac/README.md`）：

```bash
cd TunnelMac
swift build
.build/debug/tunnel-cli --server wss://frp.xxx.com/tunnel --token <同服务端> \
  --client-id mac-alice --config example-config.json
```

**客户端图形界面**：

```bash
cd TunnelMac
./build-app.sh        # 产物：TunnelApp.app，双击运行（macOS 14+）
```

## v1 范围与限制

* 仅 HTTP 穿透（子域名 + 子路由），原生 TCP/UDP 未做
* 访客请求体 ≤ 5MB，回包 ≤ 8MB，单请求等待 ≤ 30s
* 单预共享 Token，无多租户；Token 只放环境变量，不要进 git
* 子路由适合支持 `basePath` 的自研服务，通用 Web 站点请用子域名

## 路线图

* 访客侧鉴权（BasicAuth / IP 白名单）、原生 TCP 隧道、自定义域名
* 服务端落盘 + 管理 API + 指标、端到端加密说明、多用户与配额

## License

MIT，见 [LICENSE](LICENSE)。
