#pragma once

#include "tunnel/Protocol.hpp"

#include <QList>
#include <QString>

namespace tunnel {

/// 客户端持久化配置（JSON 文件，跨平台）
struct ClientConfig {
    QString serverUrl = QStringLiteral("wss://frp.example.com/tunnel");
    QString apiToken;
    QString clientId = QStringLiteral("client");
    QString baseDomain = QStringLiteral("frp.example.com");
    bool autoReconnect = true;
    QList<TunnelDef> tunnels;

    static ClientConfig load(const QString &path);
    bool save(const QString &path) const;

    /// 从 ws://host:port/tunnel 推导 http(s)://host:port
    static QString httpBaseFromWs(const QString &wsUrl);

    QMap<QString, QString> localMap() const;
};

} // namespace tunnel
