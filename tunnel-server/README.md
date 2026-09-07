# tunnel-server (Rust)

v1：HTTP 内网穿透，单端口 + Caddy 反代，子域名 + 子路由，多隧道复用一条 WSS。

## 运行

```bash
PORT=18080 SERVER_TOKEN=换成强随机 BASE_DOMAIN=frp.xxx.com cargo run
```

* 只监听 `127.0.0.1`，公网经 Caddy 进来（见 `Caddyfile.example`）
* 健康检查：`GET /healthz` -> `ok`
* 控制通道：`GET /tunnel`，需 `Authorization: Bearer <SERVER_TOKEN>`

## 协议（v0，Text JSON）

C->S `register` / `pong` / `response`，S->C `register_ack` / `ping` / `open_stream`。
访客 HTTP body 限 5MB，回包限 8MB，访客等待超时 30s。见 `src/protocol.rs`。
