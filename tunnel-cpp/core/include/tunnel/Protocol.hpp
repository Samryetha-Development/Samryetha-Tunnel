#pragma once

#include <QByteArray>
#include <QList>
#include <QMap>
#include <QMetaType>
#include <QString>

namespace tunnel {

/// 一条隧道定义（客户端配置里的一项）
struct TunnelDef {
    QString tunnelId;
    QString proto = "http"; // http | tcp
    QString subdomain;
    QString pathPrefix;
    QString localAddr; // 127.0.0.1:8080
};

/// 服务端确认后的生效隧道
struct EffectiveTunnel {
    QString tunnelId;
    QString proto;
    QString publicUrl;
    int publicPort = 0;
};

/// 服务端下发的开启流请求
struct OpenStreamMsg {
    quint64 streamId = 0;
    QString tunnelId;
    QString proto;
    QString method;
    QString path;
    QMap<QString, QString> headers;
    QByteArray body;
};

enum class MsgType {
    Unknown,
    // 服务端 -> 客户端
    RegisterAck,
    Ping,
    OpenStream,
    CloseStream,
    ServerChunk, // 上行数据（TCP 双向）
    // 客户端 -> 服务端（解析用于完整性与测试）
    Register,
    Pong,
    ResponseHead,
    ClientChunk,
    End,
    Abort,
    MgmtResp,
};

struct ParsedMsg {
    MsgType type = MsgType::Unknown;
    bool ackOk = false;
    QString ackError;
    QList<EffectiveTunnel> tunnels;
    OpenStreamMsg openStream;
    quint64 streamId = 0;
    QByteArray chunk;
    // mgmt_resp
    quint64 reqId = 0;
    int status = 0;
    QByteArray respBody;
};

QString buildRegister(const QString &clientId, const QList<TunnelDef> &tunnels);
QString buildPong();
QString buildResponseHead(quint64 streamId, int status, const QMap<QString, QString> &headers);
QString buildChunk(quint64 streamId, const QByteArray &data);
QString buildEnd(quint64 streamId);
QString buildAbort(quint64 streamId, const QString &reason);

ParsedMsg parseServerMessage(const QString &json);

/// 管理请求帧：走控制通道，无需公网管理 API
QString buildMgmt(quint64 reqId, const QString &method, const QString &path, const QByteArray &body);

} // namespace tunnel

Q_DECLARE_METATYPE(tunnel::EffectiveTunnel)
Q_DECLARE_METATYPE(QList<tunnel::EffectiveTunnel>)
