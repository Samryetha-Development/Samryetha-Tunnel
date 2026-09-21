# tunnel-server (Rust)

内网穿透服务端 v2：单端口 + Caddy 反代，HTTP 子域名/子路由 + 纯 IP TCP 中转，
多用户、OIDC/dev 登录、API Token、配额限流、访客鉴权、审计。
**不提供网页控制台** —— 全部管理操作通过 CLI / GUI 完成。

## 运行

```bash
cp .env.example .env   # 按需修改
cargo run --release
```

角色（`ROLE`）：

| 角色 | 用途 | 监听 |
| --- | --- | --- |
| `server` | 带域名，跑在 Caddy 后面，处理 HTTP 隧道 + 控制通道 | `127.0.0.1:PORT` |
| `relay` | 纯 IP，无域名，TCP 隧道端口池 | `0.0.0.0:PORT` + `TCP_PORT_START..END` |

关键配置见 `.env.example`，几个容易忽略的：

* `PATH_NS`：路径隧道命名空间。留空为 `/<用户名>/<隧道>`；设为 `/u` 则为 `/u/<用户名>/<隧道>`
  （用于复用已经存在的反代规则，例如 Caddy 已把 `/u/*` 转发到本服务）
* `AUTH_MODE=oidc` 时必须有 `OIDC_ISSUER/CLIENT_ID/CLIENT_SECRET/REDIRECT_URI`
* `ADMIN_EMAILS`：命中者自动成为管理员（否则没人能执行管理操作）
* `COOKIE_SECURE=true`（公网 HTTPS）

## 路由

| 路径 | 用途 |
| --- | --- |
| `GET /tunnel` | 控制通道（WebSocket）。`?v=3` 走私有二进制协议，缺省 JSON 兼容 |
| `GET /healthz` | 健康检查 |
| `/auth/*` | 登录：`/auth/device/*`（SSO 设备码）、`/auth/dev-*`（dev 模式）、`/auth/me`、`/auth/config` |
| `/api/*` | 管理 API：Token、隧道、用量 |
| `/api/admin/*` | 管理员 API：用户、全部隧道、审计 |
| `/{PATH_NS}/{用户名}/{隧道}/…` | 访客流量（HTTP 路径隧道） |
| `/{用户名}-{子域名}.{BASE_DOMAIN}` | 访客流量（子域名隧道，需要泛解析 + 泛域名证书） |

`/` 只返回一行纯文本说明（没有网页控制台）。

## 管理方式（CLI / GUI，走隧道通道）

管理指令通过**控制通道**（`/tunnel` 这条 WebSocket）执行，服务端**不需要**对外暴露 `/api`。
客户端既是使用端也是管理端；`/api/admin/*` 只有管理员能调用。
首次登录用设备码（匿名连接只允许 `/auth/device/*`）。

### CLI / GUI

**CLI**（`tunnel-lite`，无 Qt 依赖）：

```bash
tunnel-lite login --base https://HOST --device          # 或 --dev <邮箱>
tunnel-lite api --base https://HOST --token T token-create macbook
tunnel-lite api --base https://HOST --token T tunnel-list
tunnel-lite api --base https://HOST --token T visitor-auth web --basic alice:secret --ips 10.0.0.0/8
tunnel-lite api --base https://HOST --token T usage
tunnel-lite api --base https://HOST --token T admin-users
tunnel-lite api --base https://HOST --token T admin-audit --limit 50
tunnel-lite api --help
```

**GUI**（`tunnel-cpp`）：隧道 / Token / 用量 / 日志 / 流量 / 管理 / 设置 七个页面，
管理 API 基址在「设置」里可配（例如经 SSH 转发时的 `http://127.0.0.1:18080`）。

若管理 API 未对公网暴露，可用 SSH 转发：

```bash
ssh -L 18080:127.0.0.1:18080 ubuntu@HOST
# CLI/GUI 的 API 基址填 http://127.0.0.1:18080
```

## 协议

* **v3（私有二进制）**：`/tunnel?v=3`。LEB128 varint + 长度前缀，无 JSON/base64。
  线格式见 [`bench/README.md`](../bench/README.md)。字节数比 JSON 少 ~54%。
* **JSON 兼容**：不带 `?v=3` 即走文本 JSON + base64，便于 `wscat` 调试。

限制：访客请求体 ≤ 5MB；单请求等待本地响应 ≤ 30s。

## 部署

见 [`docs/deploy.md`](../docs/deploy.md)（含 Caddy `flush_interval -1`、BBR、systemd）。
