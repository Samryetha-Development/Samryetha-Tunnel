import SwiftUI

struct ContentView: View {
    @ObservedObject var store: TunnelStore
    @State private var section: AppSection = .dashboard

    var body: some View {
        NavigationSplitView {
            SidebarView(store: store, section: $section)
                .navigationSplitViewColumnWidth(min: 210, ideal: 230, max: 260)
        } detail: {
            Group {
                switch section {
                case .dashboard: DashboardView(store: store)
                case .logs: LogsView(store: store)
                case .stats: StatsView(store: store)
                case .settings: SettingsView(store: store)
                }
            }
            .navigationTitle(section.title)
            .toolbar {
                ToolbarItem(placement: .primaryAction) {
                    if store.running {
                        Button {
                            store.disconnect()
                        } label: {
                            Label("断开", systemImage: "stop.fill")
                        }
                        .tint(.red)
                    } else {
                        Button {
                            store.connect()
                        } label: {
                            Label("连接", systemImage: "play.fill")
                        }
                        .disabled(store.enabledTunnels.isEmpty)
                        .keyboardShortcut("r", modifiers: .command)
                    }
                }
            }
        }
        .frame(minWidth: 920, minHeight: 580)
    }
}
