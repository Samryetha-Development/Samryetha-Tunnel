import SwiftUI
import ServiceManagement

struct SettingsView: View {
    @ObservedObject var store: TunnelStore
    @State private var showToken = false
    @State private var loginEnabled = false
    @State private var loginNote: String?

    var body: some View {
        Form {
            Section("服务端") {
                TextField("控制通道地址", text: $store.serverURL)
                    .monospaced()
                    .help("如 wss://frp.xxx.com/tunnel，本地联调可用 ws://127.0.0.1:18080/tunnel")
                TextField("主域名", text: $store.baseDomain)
                    .monospaced()
                    .help("用于拼公网地址和路由，如 frp.xxx.com")
                HStack {
                    if showToken {
                        TextField("Token", text: $store.token).monospaced()
                    } else {
                        SecureField("Token", text: $store.token).monospaced()
                    }
                    Button(showToken ? "隐藏" : "显示") { showToken.toggle() }
                        .buttonStyle(.link)
                }
                Text("Token 与服务端 SERVER_TOKEN 一致即可。v1 存本机偏好设置，v2 再进钥匙串。")
                    .font(.system(size: 11))
                    .foregroundStyle(.tertiary)
                TextField("客户端 ID", text: $store.clientId)
                    .monospaced()
                    .help("服务端用它区分你的路由表，重连时恢复")
            }

            Section("行为") {
                Toggle("断线自动重连（指数退避，最大 30 秒）", isOn: $store.autoReconnect)
                Toggle("开机自动启动", isOn: $loginEnabled)
                    .onChange(of: loginEnabled) { applyLoginItem($0) }
                if let note = loginNote {
                    Text(note)
                        .font(.system(size: 11))
                        .foregroundStyle(.secondary)
                }
            }

            Section("数据") {
                HStack {
                    Text("偏好设置")
                    Spacer()
                    Text("UserDefaults · 自动保存")
                        .foregroundStyle(.secondary)
                }
                HStack {
                    Text("日志 / 事件")
                    Spacer()
                    Text("\(store.logs.count) 条日志 · \(store.events.count) 次请求")
                        .foregroundStyle(.secondary)
                        .monospacedDigit()
                    Button("清空") {
                        store.clearLogs()
                        store.events.removeAll()
                    }
                    .buttonStyle(.link)
                }
            }
        }
        .formStyle(.grouped)
        .onChange(of: store.serverURL) { store.save() }
        .onChange(of: store.token) { store.save() }
        .onChange(of: store.clientId) { store.save() }
        .onChange(of: store.baseDomain) { store.save() }
        .onChange(of: store.autoReconnect) { store.save() }
        .onAppear { refreshLoginItem() }
    }

    private func refreshLoginItem() {
        if #available(macOS 13, *) {
            loginEnabled = SMAppService.mainApp.status == .enabled
        }
    }

    private func applyLoginItem(_ on: Bool) {
        if #available(macOS 13, *) {
            do {
                if on {
                    try SMAppService.mainApp.register()
                    loginNote = "已开启，下次登录自动启动。"
                } else {
                    try SMAppService.mainApp.unregister()
                    loginNote = "已关闭。"
                }
            } catch {
                loginNote = "设置失败：\(error.localizedDescription)"
                refreshLoginItem()
            }
        } else {
            loginNote = "需要 macOS 13+"
            loginEnabled = false
        }
    }
}
