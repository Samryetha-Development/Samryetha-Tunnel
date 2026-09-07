import SwiftUI
import TunnelCore

/// 新增 / 编辑隧道。originalId == nil 表示新增。
struct TunnelEditor: View {
    @ObservedObject var store: TunnelStore
    let originalId: String?
    @State var draft: TunnelConfig
    var onDone: () -> Void

    init(store: TunnelStore, originalId: String?, initial: TunnelConfig, onDone: @escaping () -> Void) {
        self.store = store
        self.originalId = originalId
        _draft = State(initialValue: initial)
        self.onDone = onDone
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 12) {
            HStack {
                Text(originalId == nil ? "新隧道" : "编辑隧道")
                    .font(.system(size: 14, weight: .semibold))
                Spacer()
                Button("取消") { onDone() }
                    .buttonStyle(.link)
            }

            Grid(alignment: .leading, horizontalSpacing: 12, verticalSpacing: 8) {
                GridRow {
                    Text("ID").gridColumnAlignment(.trailing)
                        .font(.system(size: 12)).foregroundStyle(.secondary)
                    TextField("如 web1（唯一）", text: $draft.tunnelId)
                        .textFieldStyle(.roundedBorder)
                        .monospaced()
                }
                GridRow {
                    Text("子域名").gridColumnAlignment(.trailing)
                        .font(.system(size: 12)).foregroundStyle(.secondary)
                    TextField("如 app1（留空则不用）", text: Binding(
                        get: { draft.subdomain ?? "" },
                        set: { draft.subdomain = $0.isEmpty ? nil : $0.lowercased() }
                    ))
                    .textFieldStyle(.roundedBorder)
                    .monospaced()
                }
                GridRow {
                    Text("子路由").gridColumnAlignment(.trailing)
                        .font(.system(size: 12)).foregroundStyle(.secondary)
                    TextField("如 /u/alice/api（留空则不用）", text: Binding(
                        get: { draft.pathPrefix ?? "" },
                        set: { draft.pathPrefix = $0.isEmpty ? nil : $0 }
                    ))
                    .textFieldStyle(.roundedBorder)
                    .monospaced()
                }
                GridRow {
                    Text("本地").gridColumnAlignment(.trailing)
                        .font(.system(size: 12)).foregroundStyle(.secondary)
                    TextField("如 127.0.0.1:8080", text: $draft.localAddr)
                        .textFieldStyle(.roundedBorder)
                        .monospaced()
                }
            }

            // 校验信息
            if !errors.isEmpty {
                VStack(alignment: .leading, spacing: 2) {
                    ForEach(errors, id: \.self) { e in
                        HStack(spacing: 5) {
                            Image(systemName: "exclamationmark.triangle")
                                .font(.system(size: 11))
                            Text(e).font(.system(size: 11))
                        }
                        .foregroundStyle(.orange)
                    }
                }
            }

            // 路由预览
            HStack(spacing: 6) {
                Image(systemName: "arrow.turn.down.right")
                    .font(.system(size: 11)).foregroundStyle(.secondary)
                Text(preview)
                    .font(.system(size: 11)).monospaced()
                    .foregroundStyle(.secondary)
                    .lineLimit(2)
                Spacer()
            }
            .padding(8)
            .background(.quaternary.opacity(0.5))
            .clipShape(RoundedRectangle(cornerRadius: 7, style: .continuous))

            HStack {
                Text("保存后需重连生效")
                    .font(.system(size: 11)).foregroundStyle(.tertiary)
                Spacer()
                Button(originalId == nil ? "添加" : "保存") { save() }
                    .buttonStyle(.borderedProminent)
                    .disabled(!errors.isEmpty || draft.tunnelId.isEmpty)
            }
        }
        .padding(2)
    }

    private var errors: [String] {
        var out: [String] = []
        if draft.tunnelId.isEmpty {
            out.append("ID 不能为空")
        } else if originalId != draft.tunnelId,
                  store.tunnels.contains(where: { $0.tunnelId == draft.tunnelId }) {
            out.append("ID 已存在，换一个")
        }
        if let s = draft.subdomain, let e = validateSubdomain(s) { out.append("子域名：\(e)") }
        if let p = draft.pathPrefix, let e = validatePathPrefix(p) { out.append("子路由：\(e)") }
        if let e = validateLocalAddr(draft.localAddr) { out.append("本地地址：\(e)") }
        if (draft.subdomain ?? "").isEmpty && (draft.pathPrefix ?? "").isEmpty {
            out.append("子域名和子路由至少填一个")
        }
        return out
    }

    private var preview: String {
        var pub_ = store.baseDomain
        if let s = draft.subdomain, !s.isEmpty { pub_ = "\(s).\(pub_)" }
        else if let p = draft.pathPrefix, !p.isEmpty { pub_ = "\(pub_)\(p)" }
        return "https://\(pub_)  →  http://\(draft.localAddr.isEmpty ? "127.0.0.1:?" : draft.localAddr)"
    }

    private func save() {
        var d = draft
        d.tunnelId = d.tunnelId.trimmingCharacters(in: .whitespaces)
        if let s = d.subdomain { d.subdomain = s.trimmingCharacters(in: .whitespaces).lowercased() }
        if var p = d.pathPrefix {
            p = p.trimmingCharacters(in: .whitespaces)
            while p.count > 1 && p.hasSuffix("/") { p.removeLast() }
            d.pathPrefix = p
        }
        d.localAddr = d.localAddr.trimmingCharacters(in: .whitespaces)
        if let oid = originalId {
            store.updateTunnel(d, originalId: oid)
        } else {
            store.addTunnel(d)
        }
        onDone()
    }
}
