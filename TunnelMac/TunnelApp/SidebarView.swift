import SwiftUI
import TunnelCore

struct SidebarView: View {
    @ObservedObject var store: TunnelStore
    @Binding var section: AppSection

    var body: some View {
        VStack(alignment: .leading, spacing: 0) {
            // 应用头
            HStack(spacing: 9) {
                ZStack {
                    RoundedRectangle(cornerRadius: 8, style: .continuous)
                        .fill(.green.opacity(0.15))
                        .frame(width: 30, height: 30)
                    Image(systemName: "cable.connector.horizontal")
                        .font(.system(size: 15, weight: .medium))
                        .foregroundStyle(.green)
                }
                VStack(alignment: .leading, spacing: 1) {
                    Text("Tunnel")
                        .font(.system(size: 13, weight: .semibold))
                    Text(store.baseDomain)
                        .font(.system(size: 10))
                        .foregroundStyle(.secondary)
                        .monospaced()
                        .lineLimit(1)
                }
            }
            .padding(.horizontal, 12)
            .padding(.top, 12)
            .padding(.bottom, 10)

            // 连接状态卡
            HStack(spacing: 8) {
                StatusDot(color: phaseColor(store.running, live: store.phase == .connected),
                          pulse: store.running && store.phase != .connected)
                VStack(alignment: .leading, spacing: 1) {
                    Text(statusTitle)
                        .font(.system(size: 12, weight: .medium))
                    Text(store.lastNote)
                        .font(.system(size: 10))
                        .foregroundStyle(.secondary)
                        .lineLimit(1)
                }
                Spacer()
            }
            .padding(9)
            .background(.quaternary.opacity(0.5))
            .clipShape(RoundedRectangle(cornerRadius: 8, style: .continuous))
            .padding(.horizontal, 10)
            .padding(.bottom, 12)

            // 导航
            SectionTitle(text: "工作区")
                .padding(.horizontal, 14)
                .padding(.bottom, 4)
            ForEach(AppSection.allCases) { s in
                Button {
                    section = s
                } label: {
                    HStack(spacing: 8) {
                        Image(systemName: s.icon)
                            .font(.system(size: 13))
                            .frame(width: 20)
                        Text(s.title)
                            .font(.system(size: 13))
                        Spacer()
                        if s == .logs && !store.logs.isEmpty {
                            Text("\(store.logs.count)")
                                .font(.system(size: 10))
                                .foregroundStyle(.secondary)
                                .padding(.horizontal, 6)
                                .padding(.vertical, 1)
                                .background(.quaternary)
                                .clipShape(Capsule())
                        }
                    }
                    .padding(.horizontal, 10)
                    .padding(.vertical, 5)
                    .background(section == s ? Color.accentColor.opacity(0.12) : .clear)
                    .foregroundStyle(section == s ? .primary : .secondary)
                    .clipShape(RoundedRectangle(cornerRadius: 7, style: .continuous))
                }
                .buttonStyle(.plain)
                .padding(.horizontal, 8)
            }

            // 隧道速览
            SectionTitle(text: "隧道 · \(store.tunnels.count)")
                .padding(.horizontal, 14)
                .padding(.top, 14)
                .padding(.bottom, 4)
            ForEach(store.tunnels, id: \.tunnelId) { t in
                HStack(spacing: 7) {
                    StatusDot(color: dotFor(t))
                    Text(t.tunnelId)
                        .font(.system(size: 12))
                        .monospaced()
                        .lineLimit(1)
                    Spacer()
                    if let r = store.runtime[t.tunnelId], r.count > 0 {
                        Text("\(r.count)")
                            .font(.system(size: 10))
                            .foregroundStyle(.secondary)
                            .monospacedDigit()
                    }
                    if !store.isEnabled(t.tunnelId) {
                        Text("停用")
                            .font(.system(size: 10))
                            .foregroundStyle(.secondary)
                    }
                }
                .padding(.horizontal, 12)
                .padding(.vertical, 3)
                .opacity(store.isEnabled(t.tunnelId) ? 1 : 0.55)
            }

            Spacer()

            // 底部：client 身份
            HStack(spacing: 7) {
                Image(systemName: "person.circle")
                    .font(.system(size: 16))
                    .foregroundStyle(.secondary)
                VStack(alignment: .leading, spacing: 0) {
                    Text(store.clientId)
                        .font(.system(size: 11, weight: .medium))
                        .monospaced()
                        .lineLimit(1)
                    Text(store.running ? "运行中 · \(store.uptimeText)" : "未连接")
                        .font(.system(size: 10))
                        .foregroundStyle(.secondary)
                        .monospacedDigit()
                }
            }
            .padding(10)
        }
    }

    private var statusTitle: String {
        if store.phase == .connected { return "已连接" }
        if store.running { return "连接中" }
        return "未连接"
    }

    private func dotFor(_ t: TunnelConfig) -> Color {
        guard store.isEnabled(t.tunnelId) else { return .secondary.opacity(0.4) }
        guard store.phase == .connected else { return .orange.opacity(0.7) }
        if let r = store.runtime[t.tunnelId], r.errCount > 0 && r.count == r.errCount { return .red }
        return .green
    }
}
