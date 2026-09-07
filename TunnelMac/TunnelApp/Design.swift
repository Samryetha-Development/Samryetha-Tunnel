import SwiftUI
import AppKit

// MARK: - 导航

enum AppSection: String, CaseIterable, Identifiable {
    case dashboard, logs, stats, settings
    var id: String { rawValue }
    var title: String {
        switch self {
        case .dashboard: return "隧道"
        case .logs: return "日志"
        case .stats: return "流量"
        case .settings: return "设置"
        }
    }
    var icon: String {
        switch self {
        case .dashboard: return "cable.connector"
        case .logs: return "terminal"
        case .stats: return "chart.xyaxis.line"
        case .settings: return "gearshape"
        }
    }
}

// MARK: - 状态圆点

struct StatusDot: View {
    var color: Color
    var pulse: Bool = false
    var body: some View {
        Circle()
            .fill(color)
            .frame(width: 8, height: 8)
            .overlay {
                if pulse {
                    Circle()
                        .stroke(color.opacity(0.5), lineWidth: 2)
                        .scaleEffect(1.6)
                        .opacity(0.6)
                }
            }
    }
}

func phaseColor(_ running: Bool, live: Bool) -> Color {
    if live { return .green }
    if running { return .orange }
    return .secondary.opacity(0.5)
}

// MARK: - 卡片 / 标题 / 数值

struct Card: ViewModifier {
    func body(content: Content) -> some View {
        content
            .padding(14)
            .background(.background)
            .clipShape(RoundedRectangle(cornerRadius: 10, style: .continuous))
            .overlay {
                RoundedRectangle(cornerRadius: 10, style: .continuous)
                    .strokeBorder(.separator.opacity(0.7), lineWidth: 1)
            }
    }
}

extension View {
    func card() -> some View { modifier(Card()) }
}

struct SectionTitle: View {
    let text: String
    var body: some View {
        Text(text.uppercased())
            .font(.system(size: 11, weight: .semibold))
            .foregroundStyle(.secondary)
            .tracking(0.4)
    }
}

struct StatTile: View {
    let label: String
    let value: String
    let icon: String
    var body: some View {
        VStack(alignment: .leading, spacing: 6) {
            HStack(spacing: 5) {
                Image(systemName: icon)
                    .font(.system(size: 11))
                    .foregroundStyle(.secondary)
                Text(label)
                    .font(.system(size: 11))
                    .foregroundStyle(.secondary)
            }
            Text(value)
                .font(.system(size: 20, weight: .semibold, design: .rounded))
                .monospacedDigit()
        }
        .frame(maxWidth: .infinity, alignment: .leading)
        .card()
    }
}

struct LevelChip: View {
    let level: LogLevel
    var body: some View {
        Text(level.label)
            .font(.system(size: 10, weight: .medium))
            .padding(.horizontal, 6)
            .padding(.vertical, 2)
            .background(chipColor.opacity(0.14))
            .foregroundStyle(chipColor)
            .clipShape(Capsule())
    }
    private var chipColor: Color {
        switch level {
        case .info: return .secondary
        case .ok: return .green
        case .warn: return .orange
        case .err: return .red
        }
    }
}

struct CopyButton: View {
    let text: String
    var body: some View {
        Button {
            NSPasteboard.general.clearContents()
            NSPasteboard.general.setString(text, forType: .string)
        } label: {
            Image(systemName: "doc.on.doc")
                .font(.system(size: 11))
                .foregroundStyle(.secondary)
        }
        .buttonStyle(.plain)
        .help("复制：\(text)")
    }
}

// MARK: - 校验

func validateSubdomain(_ s: String) -> String? {
    if s.isEmpty { return nil }
    if s.count > 63 { return "不能超过 63 字符" }
    let allowed = CharacterSet.lowercaseLetters.union(.decimalDigits).union(CharacterSet(charactersIn: "-"))
    if s.lowercased().rangeOfCharacter(from: allowed.inverted) != nil { return "只允许小写字母、数字、连字符" }
    if s.hasPrefix("-") || s.hasSuffix("-") { return "不能以连字符开头或结尾" }
    return nil
}

func validatePathPrefix(_ s: String) -> String? {
    if s.isEmpty { return nil }
    if !s.hasPrefix("/") { return "必须以 / 开头" }
    if s.contains(" ") { return "不能包含空格" }
    if s != "/" && s.hasSuffix("/") { return "尾部多余的 / 会被自动去掉" }
    return nil
}

func validateLocalAddr(_ s: String) -> String? {
    let parts = s.split(separator: ":")
    if parts.count != 2 { return "格式应为 host:port，如 127.0.0.1:8080" }
    if Int(parts[1]) == nil { return "端口必须是数字" }
    let port = Int(parts[1]) ?? 0
    if port < 1 || port > 65535 { return "端口范围 1–65535" }
    return nil
}

func formatBytes(_ b: Int64) -> String {
    if b < 1024 { return "\(b)B" }
    if b < 1024 * 1024 { return String(format: "%.1fKB", Double(b) / 1024) }
    if b < 1024 * 1024 * 1024 { return String(format: "%.1fMB", Double(b) / 1024 / 1024) }
    return String(format: "%.2fGB", Double(b) / 1024 / 1024 / 1024)
}
