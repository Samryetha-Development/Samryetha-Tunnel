# tunnel-cpp

跨平台 C++ 客户端：**同一份代码在 macOS 和 Windows 编译**，对接 `tunnel-server` v2。

包含三部分：

| 目标 | 说明 |
| --- | --- |
| `tunnel_core` | 静态库：协议、控制通道、本地转发、REST/登录 |
| `tunnel-cli` | 命令行客户端（服务器/无界面/调试） |
| `tunnel-gui` | Qt 6 Widgets 桌面应用（暗色主题，仪表盘/日志/流量/设置） |

> 原来的 Swift 客户端 `TunnelMac/` 已停止维护，保留仅为历史参考。

## 依赖

- CMake ≥ 3.21
- C++20 编译器（Apple clang / MSVC 2022）
- Qt 6.5+，模块：`Core` `Network` `Widgets` `WebSockets` `Charts`

## macOS 构建

```bash
brew install qt
cd tunnel-cpp
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release   # 会自动探测 Homebrew 的 Qt
cmake --build build -j
```

产物：
- `build/cli/tunnel-cli`
- `build/gui/tunnel-gui.app`

不需要 GUI 时加 `-DBUILD_GUI=OFF`（就不必装 `Widgets`/`Charts`）。

## Windows 构建

1. 装 [Qt 在线安装器](https://www.qt.io/download-qt-installer)，选 **MSVC 2022 64-bit**，组件勾上 **Qt WebSockets** 和 **Qt Charts**（安装器自带 OpenSSL，`wss://` 才可用）。
2. 装 Visual Studio 2022（含「使用 C++ 的桌面开发」）与 CMake。
3. 构建：

```powershell
cd tunnel-cpp
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 `
      -DCMAKE_PREFIX_PATH="C:/Qt/6.11.2/msvc2022_64"
cmake --build build --config Release
```

产物：`build\cli\Release\tunnel-cli.exe`、`build\gui\Release\tunnel-gui.exe`。

## 用法

CLI（本地 dev 模式联调）：

```bash
tunnel-cli --server ws://127.0.0.1:18090/tunnel --dev-token you@example.com \
           --tunnel id=web,path=/web,local=127.0.0.1:8080 \
           --tunnel id=sse,path=/sse,local=127.0.0.1:3000
```

生产（SSO 设备码登录，Token 会缓存到配置文件）：

```bash
tunnel-cli --server wss://frp.example.com/tunnel --device-login \
           --config client.json
```

隧道参数格式：`id=ID,proto=http|tcp,sub=子域名,path=/子路由,local=host:port`。
更多参数见 `tunnel-cli --help`。

GUI：直接打开 `tunnel-gui.app` / `tunnel-gui.exe`，在「设置」里填服务端地址，点 **SSO 设备码登录** 或粘贴 API Token，保存后回「隧道」页点连接。配置自动存到系统配置目录（`client.json`）。

## 配置格式（client.json）

```json
{
  "server_url": "wss://frp.example.com/tunnel",
  "api_token": "tun_xxx",
  "client_id": "mac-1",
  "base_domain": "frp.example.com",
  "auto_reconnect": true,
  "tunnels": [
    { "tunnel_id": "web", "proto": "http", "path_prefix": "/web", "local_addr": "127.0.0.1:8080" },
    { "tunnel_id": "ssh", "proto": "tcp", "local_addr": "127.0.0.1:22" }
  ]
}
```

## 与协议 v2 的对应

- 认证：`Authorization: Bearer <API Token>`，Token 由 `/auth/device/*`（SSO 设备码）或 `/auth/dev-token`（dev 模式）获取
- 控制通道：`GET /tunnel` 升级 WebSocket，`register` / `register_ack` / `ping` / `pong`
- 访客流：`open_stream` → `response_head` + `chunk`× + `end`（HTTP 流式，SSE 不被缓冲）
- TCP 双向：服务端 `chunk` 下行、客户端 `chunk` 上行，`close_stream` 收尾
