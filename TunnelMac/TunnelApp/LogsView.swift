import SwiftUI
import AppKit

struct LogsView: View {
    @ObservedObject var store: TunnelStore
    @State private var level: LogLevel?
    @State private var source = "全部"
    @State private var query = ""
    @State private var autoScroll = true

    private static let timeFmt: DateFormatter = {
        let f = DateFormatter()
        f.dateFormat = "HH:mm:ss"
        return f
    }()

    var body: some View {
        VStack(spacing: 0) {
            // 过滤条
            HStack(spacing: 10) {
                Picker("级别", selection: $level) {
                    Text("全部").tag(nil as LogLevel?)
                    ForEach(LogLevel.allCases) { l in
                        Text(l.label).tag(l as LogLevel?)
                    }
                }
                .pickerStyle(.segmented)
                .frame(width: 260)

                Picker("来源", selection: $source) {
                    Text("全部").tag("全部")
                    Text("net").tag("net")
                    Text("app").tag("app")
                    ForEach(store.tunnels, id: \.tunnelId) { t in
                        Text(t.tunnelId).tag(t.tunnelId)
                    }
                }
                .pickerStyle(.menu)
                .frame(width: 130)

                TextField("搜索日志", text: $query)
                    .textFieldStyle(.roundedBorder)
                    .frame(maxWidth: 220)

                Spacer()

                Toggle("自动滚", isOn: $autoScroll)
                    .font(.system(size: 11))
                    .toggleStyle(.switch)
                    .controlSize(.small)
                Button("复制") { copyAll() }
                    .font(.system(size: 12))
                Button("导出") { export() }
                    .font(.system(size: 12))
                Button("清空") { store.clearLogs() }
                    .font(.system(size: 12))
            }
            .padding(12)

            Divider()

            // 列表
            ScrollViewReader { proxy in
                ScrollView {
                    LazyVStack(alignment: .leading, spacing: 0) {
                        ForEach(filtered) { e in
                            HStack(alignment: .firstTextBaseline, spacing: 8) {
                                Text(Self.timeFmt.string(from: e.at))
                                    .font(.system(size: 11, design: .monospaced))
                                    .foregroundStyle(.tertiary)
                                    .frame(width: 58, alignment: .leading)
                                LevelChip(level: e.level)
                                Text(e.source)
                                    .font(.system(size: 11, design: .monospaced))
                                    .foregroundStyle(.secondary)
                                    .frame(width: 70, alignment: .leading)
                                    .lineLimit(1)
                                Text(e.message)
                                    .font(.system(size: 12, design: .monospaced))
                                    .textSelection(.enabled)
                                    .frame(maxWidth: .infinity, alignment: .leading)
                            }
                            .padding(.horizontal, 12)
                            .padding(.vertical, 3)
                            Divider().opacity(0.3)
                        }
                        if filtered.isEmpty {
                            VStack(spacing: 6) {
                                Image(systemName: "terminal")
                                    .font(.system(size: 24))
                                    .foregroundStyle(.tertiary)
                                Text(query.isEmpty ? "暂无日志，连接后这里会实时输出" : "没有匹配的日志")
                                    .font(.system(size: 12))
                                    .foregroundStyle(.secondary)
                            }
                            .frame(maxWidth: .infinity)
                            .padding(.vertical, 50)
                        }
                        Color.clear.frame(height: 1).id("bottom")
                    }
                }
                .onChange(of: store.logs.count) {
                    if autoScroll {
                        withAnimation(.easeOut(duration: 0.15)) {
                            proxy.scrollTo("bottom", anchor: .bottom)
                        }
                    }
                }
            }
        }
    }

    private var filtered: [LogEntry] {
        store.logs.filter { e in
            if let l = level, e.level != l { return false }
            if source != "全部" && e.source != source { return false }
            if !query.isEmpty && !e.message.localizedCaseInsensitiveContains(query) { return false }
            return true
        }
    }

    private func copyAll() {
        NSPasteboard.general.clearContents()
        NSPasteboard.general.setString(store.logText(), forType: .string)
    }

    private func export() {
        let panel = NSSavePanel()
        panel.allowedContentTypes = [.plainText]
        panel.nameFieldStringValue = "tunnel-\(Int(Date().timeIntervalSince1970)).log"
        if panel.runModal() == .OK, let url = panel.url {
            try? store.logText().write(to: url, atomically: true, encoding: .utf8)
        }
    }
}
