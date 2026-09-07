import SwiftUI
import TunnelCore

struct DashboardView: View {
    @ObservedObject var store: TunnelStore
    @State private var editing: TunnelConfig?
    @State private var isNew = false
    @State private var showAdd = false

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 14) {
                // 顶栏：状态 + 操作
                HStack(alignment: .center) {
                    VStack(alignment: .leading, spacing: 2) {
                        HStack(spacing: 8) {
                            StatusDot(color: phaseColor(store.running, live: store.phase == .connected),
                                      pulse: store.running && store.phase != .connected)
                            Text(headerTitle)
                                .font(.system(size: 17, weight: .semibold))
                        }
                        Text(headerSub)
                            .font(.system(size: 12))
                            .foregroundStyle(.secondary)
                            .monospaced()
                    }
                    Spacer()
                    Button("测试本地") { store.testAllLocals() }
                        .buttonStyle(.link)
                        .font(.system(size: 12))
                    Button(showAdd ? "收起" : "＋ 新隧道") { showAdd.toggle() }
                    if store.running {
                        Button("断开") { store.disconnect() }
                            .buttonStyle(.bordered)
                            .tint(.red)
                    } else {
                        Button("连接") { store.connect() }
                            .buttonStyle(.borderedProminent)
                            .keyboardShortcut(.return, modifiers: .command)
                            .disabled(store.enabledTunnels.isEmpty)
                    }
                }

                if showAdd {
                    TunnelEditor(store: store, originalId: nil, initial: TunnelConfig(tunnelId: "", subdomain: "", localAddr: "127.0.0.1:8080")) {
                        showAdd = false
                    }
                    .card()
                }

                // 指标行
                HStack(spacing: 10) {
                    StatTile(label: "在线隧道", value: "\(store.enabledTunnels.count)/\(store.tunnels.count)", icon: "cable.connector")
                    StatTile(label: "累计请求", value: "\(store.totalRequests)", icon: "arrow.left.arrow.right")
                    StatTile(label: "成功率", value: store.totalRequests == 0 ? "—" : String(format: "%.1f%%", store.okRate * 100), icon: "checkmark.circle")
                    StatTile(label: "下行流量", value: formatBytes(store.totalBytes), icon: "arrow.down.circle")
                }

                // 隧道卡片
                if store.tunnels.isEmpty {
                    VStack(spacing: 8) {
                        Image(systemName: "cable.connector.slash")
                            .font(.system(size: 28))
                            .foregroundStyle(.tertiary)
                        Text("还没有隧道")
                            .font(.system(size: 14, weight: .medium))
                        Text("点右上「新隧道」把内网服务暴露出去，一条隧道对应一个子域名或子路由。")
                            .font(.system(size: 12))
                            .foregroundStyle(.secondary)
                    }
                    .frame(maxWidth: .infinity)
                    .padding(.vertical, 40)
                    .card()
                } else {
                    LazyVGrid(columns: [GridItem(.adaptive(minimum: 330), spacing: 12)], spacing: 12) {
                        ForEach(store.tunnels, id: \.tunnelId) { t in
                            TunnelCard(store: store, tunnel: t,
                                       onEdit: { editing = t; isNew = false })
                        }
                    }
                }
            }
            .padding(18)
        }
        .sheet(item: $editing) { t in
            TunnelEditor(store: store, originalId: t.tunnelId, initial: t) {
                editing = nil
            }
            .frame(minWidth: 440)
            .padding(4)
        }
    }

    private var headerTitle: String {
        if store.phase == .connected { return "控制通道正常" }
        if store.running { return "正在建立控制通道…" }
        return "控制通道未建立"
    }

    private var headerSub: String {
        "\(store.serverURL) · \(store.clientId)"
    }
}

// MARK: - 单隧道卡片

struct TunnelCard: View {
    @ObservedObject var store: TunnelStore
    let tunnel: TunnelConfig
    var onEdit: () -> Void
    @State private var showDelete = false

    var body: some View {
        VStack(alignment: .leading, spacing: 10) {
            HStack(spacing: 8) {
                StatusDot(color: dot, pulse: store.running && store.phase != .connected && enabled)
                Text(tunnel.tunnelId)
                    .font(.system(size: 14, weight: .semibold))
                    .monospaced()
                Spacer()
                Toggle("", isOn: Binding(
                    get: { enabled },
                    set: { _ in store.toggleEnabled(tunnel.tunnelId) }
                ))
                .toggleStyle(.switch)
                .controlSize(.small)
                .help(enabled ? "停用（重连后生效）" : "启用")
            }

            // 公网路由
            HStack(spacing: 6) {
                Image(systemName: tunnel.subdomain?.isEmpty == false ? "globe" : "folder")
                    .font(.system(size: 11))
                    .foregroundStyle(.secondary)
                Text(store.routeText(for: tunnel))
                    .font(.system(size: 12))
                    .monospaced()
                    .lineLimit(1)
                    .truncationMode(.middle)
                CopyButton(text: store.publicURL(for: tunnel))
            }
            // 本地目标
            HStack(spacing: 6) {
                Image(systemName: "house")
                    .font(.system(size: 11))
                    .foregroundStyle(.secondary)
                Text(tunnel.localAddr)
                    .font(.system(size: 12))
                    .monospaced()
                if let note = store.localCheck[tunnel.tunnelId] {
                    Text("· \(note)")
                        .font(.system(size: 11))
                        .foregroundStyle(note.contains("不通") || note.contains("非法") ? .red : .green)
                        .monospacedDigit()
                }
                Spacer()
                MiniBars(events: store.runtime[tunnel.tunnelId]?.recent ?? [])
            }

            Divider().opacity(0.5)

            HStack(spacing: 4) {
                Button("本地测试") { store.testLocal(tunnel) }
                    .buttonStyle(.link).font(.system(size: 11))
                Button("打开公网") {
                    if let url = URL(string: store.publicURL(for: tunnel)) {
                        NSWorkspace.shared.open(url)
                    }
                }
                .buttonStyle(.link).font(.system(size: 11))
                .disabled(store.phase != .connected)
                Spacer()
                if let r = store.runtime[tunnel.tunnelId], r.count > 0 {
                    Text("\(r.count)次 · \(formatBytes(r.bytes)) · \(r.latencyMs.map { "\($0)ms" } ?? "—")")
                        .font(.system(size: 10))
                        .foregroundStyle(.secondary)
                        .monospacedDigit()
                }
                Button("编辑") { onEdit() }
                    .buttonStyle(.link).font(.system(size: 11))
                Menu("更多") {
                    Button("复制公网地址") {
                        NSPasteboard.general.clearContents()
                        NSPasteboard.general.setString(store.publicURL(for: tunnel), forType: .string)
                    }
                    Button("复制一条") { store.duplicateTunnel(tunnel.tunnelId) }
                    Divider()
                    Button("删除", role: .destructive) { showDelete = true }
                }
                .menuStyle(.borderlessButton)
                .font(.system(size: 11))
            }
        }
        .card()
        .opacity(enabled ? 1 : 0.6)
        .confirmationDialog("删除隧道 \(tunnel.tunnelId)？", isPresented: $showDelete, titleVisibility: .visible) {
            Button("删除", role: .destructive) { store.removeTunnel(tunnel.tunnelId) }
            Button("取消", role: .cancel) {}
        }
    }

    private var enabled: Bool { store.isEnabled(tunnel.tunnelId) }

    private var dot: Color {
        guard enabled else { return .secondary.opacity(0.4) }
        guard store.phase == .connected else { return .orange.opacity(0.8) }
        if let r = store.runtime[tunnel.tunnelId], r.count > 0, r.errCount == r.count { return .red }
        return .green
    }
}

// 最近 30 次请求迷你条：绿=2xx/3xx，红=5xx，灰=4xx
struct MiniBars: View {
    let events: [RequestEvent]
    var body: some View {
        HStack(spacing: 2) {
            ForEach(Array(events.suffix(30).enumerated()), id: \.offset) { _, ev in
                RoundedRectangle(cornerRadius: 1)
                    .fill(barColor(ev.status))
                    .frame(width: 3, height: barHeight(ev))
            }
            if events.isEmpty {
                Text("暂无流量")
                    .font(.system(size: 10))
                    .foregroundStyle(.tertiary)
            }
        }
        .frame(height: 16)
    }
    private func barColor(_ s: Int) -> Color {
        if s >= 500 { return .red }
        if s >= 400 { return .gray }
        return .green
    }
    private func barHeight(_ ev: RequestEvent) -> CGFloat {
        let ms = max(ev.latencyMs, 1)
        return min(16, max(4, CGFloat(ms) / 50))
    }
}

