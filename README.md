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
| `tunnel-server/` | Rust 服务端（tokio + axum）：控制通道、多租户、OIDC 登录、配额审计、访客分发、纯 IP TCP 中转 |
| `tunnel-cpp/` | 跨平台 C++ 客户端（同一份代码编 macOS/Windows）：核心静态库 + CLI + Qt 6 图形界面 |
| `tunnel-lite/` | **无 Qt 依赖**的轻量 CLI（~160KB、~2–3MB 内存），适合服务器/容器常驻 |
| `TunnelMac/` | 旧 Swift 客户端，**已停止维护**，仅作历史参考 |

## 快速开始

**服务端**（见 `tunnel-server/README.md`）：

```bash
cd tunnel-server
PORT=18080 SERVER_TOKEN=换成强随机串 BASE_DOMAIN=frp.xxx.com cargo run --release
```

Caddy 配置见 `tunnel-server/Caddyfile.example`（主域名 + 泛域名反代到 `127.0.0.1:18080`）。

**客户端（C++，跨平台）**：

```bash
cd tunnel-cpp
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release   # macOS 自动探测 Homebrew Qt
cmake --build build -j
./build/cli/tunnel-cli --server ws://127.0.0.1:18090/tunnel --dev-token you@example.com \
  --tunnel id=web,path=/web,local=127.0.0.1:8080
```

Windows 构建与 GUI 用法见 `tunnel-cpp/README.md`。

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
