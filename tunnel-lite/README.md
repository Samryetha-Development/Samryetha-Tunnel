# tunnel-lite

**不依赖 Qt** 的轻量跨平台客户端（macOS / Linux / Windows），同一份 C++17 代码。
目标是把系统占用压到最低，适合服务器、容器、长期常驻。

| 指标 | tunnel-lite | tunnel-cpp CLI（Qt） |
| --- | --- | --- |
| 依赖 | 无（可选 OpenSSL 支持 wss） | Qt 6 Core/Network/WebSockets |
| 可执行文件 | ~160 KB | ~280 KB + 一堆 Qt 动态库 |
| 内存占用 | ~2–3 MB | ~30–60 MB |

## 依赖

- CMake ≥ 3.16、C++17 编译器
- 可选：OpenSSL（只有 `wss://` 需要）

## 构建

```bash
cd tunnel-lite
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
# 产物：build/tunnel-lite
```

需要 `wss://`（HTTPS 域名服务端）时：

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DTUNNEL_LITE_TLS=ON
cmake --build build -j
```

纯 IP 中转（`ws://47.103.21.5:端口/tunnel`）**不需要** TLS，默认构建即可，体积最小。

## 用法

```bash
# dev 模式联调
tunnel-lite --server ws://127.0.0.1:18090/tunnel --dev-token you@example.com \
            --tunnel id=web,path=/web,local=127.0.0.1:8080 \
            --tunnel id=sse,path=/sse,local=127.0.0.1:3000

# 生产：直接用 API Token（服务端 Web 控制台「API Token」页生成）
tunnel-lite --server wss://frp.example.com/tunnel --token tun_xxx \
            --client-id srv-1 --config /etc/tunnel/client.json

# 纯 IP 中转 TCP（如 SSH）
tunnel-lite --server ws://47.103.21.5:18091/tunnel --token tun_xxx \
            --tunnel id=ssh,proto=tcp,local=127.0.0.1:22
```

隧道参数：`id=ID,proto=http|tcp,sub=子域名,path=/子路由,local=host:port`（`--tunnel` 可重复）。

## 配置文件（可选）

`client.json`（与 Qt 客户端同格式）：

```json
{
  "server_url": "ws://47.103.21.5:18091/tunnel",
  "api_token": "tun_xxx",
  "client_id": "srv-1",
  "tunnels": [
    { "tunnel_id": "ssh", "proto": "tcp", "local_addr": "127.0.0.1:22" },
    { "tunnel_id": "web", "proto": "http", "path_prefix": "/web", "local_addr": "127.0.0.1:8080" }
  ]
}
```

## 常驻（systemd 示例）

```ini
[Unit]
Description=tunnel-lite
After=network-online.target

[Service]
ExecStart=/usr/local/bin/tunnel-lite --config /etc/tunnel/client.json
Restart=always
RestartSec=3
DynamicUser=yes

[Install]
WantedBy=multi-user.target
```

## 说明

- 断线指数退避重连（1s → 30s），`--no-reconnect` 可关闭
- HTTP 流式回传（SSE 不被缓冲），TCP 双向转发，本地连接建立前的早到数据会缓存
- 目前 `wss://` 需 `-DTUNNEL_LITE_TLS=ON`；`ws://` 开箱即用
