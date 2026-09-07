# TunnelMac

Swift 客户端：`TunnelCore`（协议+长连接）+ `tunnel-cli`（联调）+ `TunnelApp/`（SwiftUI 完整 App）。

## 先用 CLI 跑通

```bash
swift build
# 本地先起个被穿透的服务，如：
python3 -m http.server 8080
# 再连服务端（本地联调时把 wss 换成 ws）：
.build/debug/tunnel-cli --server ws://127.0.0.1:18080/tunnel --token test123 --client-id mac-alice --config example-config.json
```

## 再开 SwiftUI App

1. Xcode 新建 macOS App 工程（SwiftUI），Bundle ID 随意
2. 把 `Sources/TunnelCore/*.swift` 拖进工程（勾选 Copy）
3. 把 `TunnelApp/*.swift` 拖进工程，替换默认的 App/View
4. 运行，填 wss 地址 + token + 隧道列表，点连接

`TunnelApp/TunnelStore.swift` 就是全部状态管理，可继续加菜单栏常驻、开机自启。
