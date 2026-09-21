#include "tunnel/Config.hpp"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>

namespace tunnel {

QString ClientConfig::httpBaseFromWs(const QString &wsUrl) {
    QUrl u(wsUrl);
    QString scheme = u.scheme();
    if (scheme == "wss")
        scheme = "https";
    else if (scheme == "ws")
        scheme = "http";
    QString path = u.path(); // 例如 /tunnel
    if (path.endsWith("/tunnel"))
        path.chop(QString("/tunnel").size());
    while (path.endsWith('/'))
        path.chop(1);
    QString base = scheme + "://" + u.host();
    if (u.port() > 0 && u.port() != (scheme == "https" ? 443 : 80))
        base += ":" + QString::number(u.port());
    base += path;
    return base;
}

QMap<QString, QString> ClientConfig::localMap() const {
    QMap<QString, QString> m;
    for (const auto &t : tunnels)
        m[t.tunnelId] = t.localAddr;
    return m;
}

ClientConfig ClientConfig::load(const QString &path) {
    ClientConfig c;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return c;
    const auto doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isObject())
        return c;
    const auto o = doc.object();
    if (o.contains("server_url"))
        c.serverUrl = o.value("server_url").toString();
    if (o.contains("api_token"))
        c.apiToken = o.value("api_token").toString();
    if (o.contains("client_id"))
        c.clientId = o.value("client_id").toString();
    if (o.contains("base_domain"))
        c.baseDomain = o.value("base_domain").toString();
    if (o.contains("auto_reconnect"))
        c.autoReconnect = o.value("auto_reconnect").toBool();
    if (o.contains("tunnels")) {
        c.tunnels.clear();
        for (const auto &v : o.value("tunnels").toArray()) {
            const auto t = v.toObject();
            TunnelDef d;
            d.tunnelId = t.value("tunnel_id").toString();
            d.proto = t.value("proto").toString("http");
            d.subdomain = t.value("subdomain").toString();
            d.pathPrefix = t.value("path_prefix").toString();
            d.localAddr = t.value("local_addr").toString();
            c.tunnels.append(d);
        }
    }
    return c;
}

bool ClientConfig::save(const QString &path) const {
    QJsonArray arr;
    for (const auto &t : tunnels) {
        QJsonObject o;
        o["tunnel_id"] = t.tunnelId;
        o["proto"] = t.proto;
        if (!t.subdomain.isEmpty())
            o["subdomain"] = t.subdomain;
        if (!t.pathPrefix.isEmpty())
            o["path_prefix"] = t.pathPrefix;
        o["local_addr"] = t.localAddr;
        arr.append(o);
    }
    QJsonObject o;
    o["server_url"] = serverUrl;
    o["api_token"] = apiToken;
    o["client_id"] = clientId;
    o["base_domain"] = baseDomain;
    o["auto_reconnect"] = autoReconnect;
    o["tunnels"] = arr;
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    f.write(QJsonDocument(o).toJson(QJsonDocument::Indented));
    return true;
}

} // namespace tunnel
