import SwiftUI
import Charts
import TunnelCore

struct StatsView: View {
    @ObservedObject var store: TunnelStore

    private static let clock: DateFormatter = {
        let f = DateFormatter()
        f.dateFormat = "HH:mm"
        return f
    }()

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 14) {
                HStack(spacing: 10) {
                    StatTile(label: "累计请求", value: "\(store.totalRequests)", icon: "arrow.left.arrow.right")
                    StatTile(label: "成功率", value: store.totalRequests == 0 ? "—" : String(format: "%.1f%%", store.okRate * 100), icon: "checkmark.circle")
                    StatTile(label: "平均延迟", value: store.events.isEmpty ? "—" : "\(store.avgLatencyMs)ms", icon: "timer")
                    StatTile(label: "下行流量", value: formatBytes(store.totalBytes), icon: "arrow.down.circle")
                }

                // 30 分钟趋势
                VStack(alignment: .leading, spacing: 8) {
                    SectionTitle(text: "最近 30 分钟 · 每分钟请求数")
                    Chart(store.bucketsLast30Min()) { b in
                        BarMark(
                            x: .value("时间", b.at, unit: .minute),
                            y: .value("请求", b.count)
                        )
                        .foregroundStyle(b.count > 0 ? .green.opacity(0.8) : .gray.opacity(0.25))
                    }
                    .chartXAxis {
                        AxisMarks(values: .stride(by: .minute, count: 10)) { v in
                            AxisValueLabel(format: .dateTime.hour().minute())
                        }
                    }
                    .frame(height: 170)
                }
                .card()

                HStack(alignment: .top, spacing: 12) {
                    // 按隧道分布
                    VStack(alignment: .leading, spacing: 8) {
                        SectionTitle(text: "按隧道分布")
                        if store.perTunnelCounts().isEmpty {
                            Text("暂无数据")
                                .font(.system(size: 12))
                                .foregroundStyle(.tertiary)
                                .frame(maxWidth: .infinity, minHeight: 120)
                        } else {
                            Chart(store.perTunnelCounts(), id: \.id) { item in
                                BarMark(
                                    x: .value("请求", item.count),
                                    y: .value("隧道", item.id)
                                )
                                .foregroundStyle(.green.opacity(0.8))
                            }
                            .frame(minHeight: CGFloat(max(120, store.perTunnelCounts().count * 34)))
                        }
                    }
                    .card()
                    .frame(maxWidth: .infinity)

                    // 最近请求
                    VStack(alignment: .leading, spacing: 8) {
                        SectionTitle(text: "最近请求 · \(min(store.events.count, 30))")
                        if store.events.isEmpty {
                            Text("暂无数据")
                                .font(.system(size: 12))
                                .foregroundStyle(.tertiary)
                                .frame(maxWidth: .infinity, minHeight: 120)
                        } else {
                            ForEach(Array(store.events.suffix(30).reversed()), id: \.at) { ev in
                                HStack(spacing: 8) {
                                    Text(Self.clock.string(from: ev.at))
                                        .font(.system(size: 11, design: .monospaced))
                                        .foregroundStyle(.tertiary)
                                    Text(ev.tunnelId)
                                        .font(.system(size: 11, design: .monospaced))
                                        .frame(width: 80, alignment: .leading)
                                        .lineLimit(1)
                                    StatusPill(status: ev.status)
                                    Spacer()
                                    Text(formatBytes(Int64(ev.bytes)))
                                        .font(.system(size: 11, design: .monospaced))
                                        .foregroundStyle(.secondary)
                                    Text("\(ev.latencyMs)ms")
                                        .font(.system(size: 11, design: .monospaced))
                                        .foregroundStyle(.secondary)
                                        .frame(width: 52, alignment: .trailing)
                                }
                                Divider().opacity(0.3)
                            }
                        }
                    }
                    .card()
                    .frame(maxWidth: .infinity)
                }
            }
            .padding(18)
        }
    }
}

struct StatusPill: View {
    let status: Int
    var body: some View {
        Text("\(status)")
            .font(.system(size: 10, weight: .semibold, design: .monospaced))
            .padding(.horizontal, 7)
            .padding(.vertical, 2)
            .background(color.opacity(0.13))
            .foregroundStyle(color)
            .clipShape(Capsule())
    }
    private var color: Color {
        if status >= 500 { return .red }
        if status >= 400 { return .orange }
        return .green
    }
}
