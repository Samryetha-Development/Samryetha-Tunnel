import SwiftUI

@main
struct TunnelApp: App {
    @StateObject private var store = TunnelStore()

    var body: some Scene {
        WindowGroup {
            ContentView(store: store)
        }
        .windowStyle(.titleBar)
        .commands {
            CommandGroup(after: .toolbar) {
                Button(store.running ? "断开连接" : "开始连接") {
                    store.running ? store.disconnect() : store.connect()
                }
                .keyboardShortcut("r", modifiers: .command)
                Divider()
                Button("测试全部本地服务") {
                    store.testAllLocals()
                }
                .keyboardShortcut("t", modifiers: .command)
            }
        }

        Settings {
            SettingsView(store: store)
                .frame(width: 520)
        }
    }
}
