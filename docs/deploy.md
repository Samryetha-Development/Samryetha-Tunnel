# 部署指南（两台服务器）

```
访客 ──HTTPS──▶ [A: 域名+Caddy] ──▶ tunnel-server (role=server)
                                        ▲
                                        │ WSS /tunnel（单 443，穿 Caddy）
                                        │
                              [客户端 macOS/Windows/Linux]

访客 ──TCP:端口──▶ [B: 47.103.21.5]  tunnel-server (role=relay) ──▶ 客户端
                    （纯 IP，无域名，TCP 端口池）
```

- **A 机**：带域名，跑 `role=server`，负责 HTTP 子域名/子路由 + Web 控制台 + OIDC 登录
- **B 机**：纯 IP（47.103.21.5），跑 `role=relay`，负责 TCP 隧道（SSH/数据库等）的端口池转发

> 只开了 TCP 端口即可，本方案不需要 UDP（QUIC 未启用）。

---

## 1. 系统层调优（A/B 都做）

### 1.1 BBR（降低延迟抖动，最重要）

Cubic 会让队列先堆满再猛降，产生锯齿状延迟；BBR 以低队列为目标，抖动明显更小。

```bash
cat | sudo tee /etc/sysctl.d/99-tunnel.conf <<'EOF'
net.core.default_qdisc = fq
net.ipv4.tcp_congestion_control = bbr
net.ipv4.tcp_notsent_lowat = 16384
net.ipv4.tcp_keepalive_time = 60
net.ipv4.tcp_keepalive_intvl = 15
net.ipv4.tcp_keepalive_probes = 4
EOF
sudo sysctl --system
sysctl net.ipv4.tcp_congestion_control   # 期望输出：net.ipv4.tcp_congestion_control = bbr
```

### 1.2 文件描述符

```bash
sudo tee /etc/security/limits.d/tunnel.conf <<'EOF'
* soft nofile 1048576
* hard nofile 1048576
EOF
```

---

## 2. A 机（域名 + Caddy，role=server）

### 2.1 Caddy

用仓库里的 `tunnel-server/Caddyfile.example`，**务必保留 `flush_interval -1`**：

```caddyfile
frp.xxx.com {
	reverse_proxy 127.0.0.1:18080 { flush_interval -1 }
}
*.frp.xxx.com {
	reverse_proxy 127.0.0.1:18080 { flush_interval -1 }
}
```

DNS 需要：`frp.xxx.com` A 记录 + `*.frp.xxx.com` 泛解析，都指向 A 机。

### 2.2 服务端

```bash
sudo mkdir -p /opt/tunnel && cd /opt/tunnel
# 上传 tunnel-server 二进制与 .env
```

`/opt/tunnel/.env`（关键项）：

```ini
ROLE=server
BIND=127.0.0.1
PORT=18080
BASE_DOMAIN=frp.xxx.com
DATABASE_URL=sqlite:///var/lib/tunnel/tunnel.db?mode=rwc
AUTH_MODE=oidc
OIDC_ISSUER=https://auth.samryetha.com
OIDC_CLIENT_ID=<你的 client id>
OIDC_CLIENT_SECRET=<你的 secret>
OIDC_REDIRECT_URI=https://frp.xxx.com/auth/callback
ADMIN_EMAILS=<你的邮箱>
COOKIE_SECURE=true
```

`/etc/systemd/system/tunnel-server.service`：

```ini
[Unit]
Description=Samryetha Tunnel (server)
After=network-online.target
Wants=network-online.target

[Service]
WorkingDirectory=/opt/tunnel
ExecStart=/opt/tunnel/tunnel-server
EnvironmentFile=/opt/tunnel/.env
Restart=always
RestartSec=2
LimitNOFILE=1048576

[Install]
WantedBy=multi-user.target
```

```bash
sudo systemctl enable --now tunnel-server
sudo systemctl status tunnel-server --no-pager
```

---

## 3. B 机（47.103.21.5，role=relay 纯 IP）

`/opt/tunnel/.env`：

```ini
ROLE=relay
PORT=18091
BASE_DOMAIN=frp.xxx.com
DATABASE_URL=sqlite:///var/lib/tunnel/tunnel.db?mode=rwc
AUTH_MODE=oidc
OIDC_ISSUER=https://auth.samryetha.com
OIDC_CLIENT_ID=<同一个 client id>
OIDC_CLIENT_SECRET=<同一个 secret>
OIDC_REDIRECT_URI=https://frp.xxx.com/auth/callback
TCP_PORT_START=10000
TCP_PORT_END=10099
RELAY_HOST=47.103.21.5
```

> `DATABASE_URL` 指向**同一份** SQLite 或换成同一个 Postgres；至少 `AUTH_MODE`/OIDC 配置要与 A 机一致，Token 才能互认。
> relay 只监听 `0.0.0.0:18091`（控制通道）与 `TCP_PORT_START..END`（隧道端口）。

安全组放行（仅 TCP）：`18091`、`10000-10099`。

systemd 同 A 机，把 `Description` 与 `ExecStart` 换成 relay 即可（同一二进制，靠 `.env` 的 `ROLE` 区分）。

---

## 4. 客户端

```bash
# 纯 IP 中转（TCP 隧道，如 SSH）
tunnel-lite --server ws://47.103.21.5:18091/tunnel --token tun_xxx \
            --proto binary --tunnel id=ssh,proto=tcp,local=127.0.0.1:22

# 域名服务端（HTTP/HTTPS）
tunnel-lite --server wss://frp.xxx.com/tunnel --token tun_xxx \
            --proto binary --tunnel id=web,path=/web,local=127.0.0.1:8080
```

- `--proto binary` 用私有二进制协议（字节数 −54%，高并发尾延迟更低）
- `wss://` 需要 `-DTUNNEL_LITE_TLS=ON` 构建（OpenSSL）

---

## 5. 验证

```bash
# A 机
curl -s https://frp.xxx.com/healthz            # ok
# B 机
curl -s http://127.0.0.1:18091/healthz         # ok
# 隧道：客户端连上后
curl -sN https://frp.xxx.com/<用户名>/<隧道>/  # SSE 应逐条实时到达，不积压
```

排错重点：SSE 卡顿 → 先看 Caddy 是否漏了 `flush_interval -1`；延迟抖动大 → 确认 BBR 已生效。
