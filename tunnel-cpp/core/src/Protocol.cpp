#include "tunnel/Protocol.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace tunnel {

static QString b64(const QByteArray &d) {
    return QString::fromLatin1(d.toBase64());
}

static QByteArray unb64(const QString &s) {
    return QByteArray::fromBase64(s.toLatin1());
}

QString buildRegister(const QString &clientId, const QList<TunnelDef> &tunnels) {
    QJsonArray arr;
    for (const auto &t : tunnels) {
        QJsonObject o;
        o["tunnel_id"] = t.tunnelId;
        o["proto"] = t.proto.isEmpty() ? QStringLiteral("http") : t.proto;
        if (!t.subdomain.isEmpty())
            o["subdomain"] = t.subdomain;
        if (!t.pathPrefix.isEmpty())
            o["path_prefix"] = t.pathPrefix;
        if (!t.localAddr.isEmpty())
            o["local_addr"] = t.localAddr;
        arr.append(o);
    }
    QJsonObject root;
    root["type"] = QStringLiteral("register");
    root["client_id"] = clientId;
    root["tunnels"] = arr;
    return QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact));
}

QString buildPong() {
    QJsonObject o;
    o["type"] = QStringLiteral("pong");
    return QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact));
}

QString buildResponseHead(quint64 streamId, int status, const QMap<QString, QString> &headers) {
    QJsonObject h;
    for (auto it = headers.constBegin(); it != headers.constEnd(); ++it)
        h[it.key()] = it.value();
    QJsonObject o;
    o["type"] = QStringLiteral("response_head");
    o["stream_id"] = static_cast<double>(streamId);
    o["status"] = status;
    o["headers"] = h;
    return QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact));
}

QString buildChunk(quint64 streamId, const QByteArray &data) {
    QJsonObject o;
    o["type"] = QStringLiteral("chunk");
    o["stream_id"] = static_cast<double>(streamId);
    o["data_b64"] = b64(data);
    return QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact));
}

QString buildEnd(quint64 streamId) {
    QJsonObject o;
    o["type"] = QStringLiteral("end");
    o["stream_id"] = static_cast<double>(streamId);
    return QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact));
}

QString buildAbort(quint64 streamId, const QString &reason) {
    QJsonObject o;
    o["type"] = QStringLiteral("abort");
    o["stream_id"] = static_cast<double>(streamId);
    o["reason"] = reason;
    return QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact));
}

QString buildMgmt(quint64 reqId, const QString &method, const QString &path, const QByteArray &body) {
    QJsonObject o;
    o["type"] = QStringLiteral("mgmt");
    o["req_id"] = static_cast<double>(reqId);
    o["method"] = method;
    o["path"] = path;
    o["body_b64"] = QString::fromLatin1(body.toBase64());
    return QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact));
}

ParsedMsg parseServerMessage(const QString &json) {
    ParsedMsg m;
    const auto doc = QJsonDocument::fromJson(json.toUtf8());
    if (!doc.isObject())
        return m;
    const auto o = doc.object();
    const QString type = o.value("type").toString();

    auto readHeaders = [](const QJsonObject &src) {
        QMap<QString, QString> h;
        for (auto it = src.constBegin(); it != src.constEnd(); ++it)
            h[it.key()] = it.value().toString();
        return h;
    };

    if (type == "register_ack") {
        m.type = MsgType::RegisterAck;
        m.ackOk = o.value("ok").toBool();
        if (o.contains("error") && !o.value("error").isNull())
            m.ackError = o.value("error").toString();
        for (const auto &v : o.value("tunnels").toArray()) {
            const auto t = v.toObject();
            EffectiveTunnel et;
            et.tunnelId = t.value("tunnel_id").toString();
            et.proto = t.value("proto").toString();
            et.publicUrl = t.value("public_url").toString();
            et.publicPort = t.value("public_port").toInt();
            m.tunnels.append(et);
        }
    } else if (type == "ping") {
        m.type = MsgType::Ping;
    } else if (type == "open_stream") {
        m.type = MsgType::OpenStream;
        auto &s = m.openStream;
        s.streamId = static_cast<quint64>(o.value("stream_id").toDouble());
        s.tunnelId = o.value("tunnel_id").toString();
        s.proto = o.value("proto").toString();
        s.method = o.value("method").toString();
        s.path = o.value("path").toString();
        s.headers = readHeaders(o.value("headers").toObject());
        const QString body = o.value("body_b64").toString();
        if (!body.isEmpty())
            s.body = unb64(body);
        m.streamId = s.streamId;
    } else if (type == "close_stream") {
        m.type = MsgType::CloseStream;
        m.streamId = static_cast<quint64>(o.value("stream_id").toDouble());
    } else if (type == "chunk") {
        m.type = MsgType::ServerChunk;
        m.streamId = static_cast<quint64>(o.value("stream_id").toDouble());
        m.chunk = unb64(o.value("data_b64").toString());
    } else if (type == "register") {
        m.type = MsgType::Register;
    } else if (type == "pong") {
        m.type = MsgType::Pong;
    } else if (type == "response_head") {
        m.type = MsgType::ResponseHead;
    } else if (type == "end") {
        m.type = MsgType::End;
        m.streamId = static_cast<quint64>(o.value("stream_id").toDouble());
    } else if (type == "mgmt_resp") {
        m.type = MsgType::MgmtResp;
        m.reqId = static_cast<quint64>(o.value("req_id").toDouble());
        m.status = o.value("status").toInt();
        m.respBody = unb64(o.value("body_b64").toString());
    } else if (type == "abort") {
        m.type = MsgType::Abort;
        m.streamId = static_cast<quint64>(o.value("stream_id").toDouble());
    }
    return m;
}

} // namespace tunnel
