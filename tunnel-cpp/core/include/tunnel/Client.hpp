#pragma once

#include "tunnel/Config.hpp"
#include "tunnel/Protocol.hpp"

#include <QElapsedTimer>
#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QTimer>

#include <functional>

class QNetworkAccessManager;
class QWebSocket;

namespace tunnel {

class Forwarder;

/// 控制通道客户端：一条 WebSocket 长连接，多隧道复用，断线指数退避重连
class Client : public QObject {
    Q_OBJECT
public:
    explicit Client(QObject *parent = nullptr);

    void setConfig(const ClientConfig &cfg);
    const ClientConfig &config() const { return cfg_; }

    void start();
    void stop();

    /// 管理请求（Token/隧道/访客鉴权/用量/管理员）通过同一条控制通道执行
    using MgmtCallback = std::function<void(bool, const QJsonObject &, const QString &)>;
    void mgmtRequest(const QString &method, const QString &path, const QJsonObject &body,
                     MgmtCallback cb);
    bool isConnected() const { return connected_; }
    QString serverHost() const { return cfg_.serverUrl; }

signals:
    void log(const QString &level, const QString &source, const QString &message);
    void stateChanged(const QString &state); // connecting | connected | disconnected
    void ack(bool ok, const QString &error, const QList<tunnel::EffectiveTunnel> &tunnels);
    void requestFinished(const QString &tunnelId, int status, qint64 bytes, int latencyMs);

private:
    struct StreamState {
        Forwarder *fwd = nullptr;
        QString tunnelId;
        QElapsedTimer timer;
        qint64 bytes = 0;
        int status = 0;
    };

    void openConnection();
    void sendText(const QString &s);
    void handleMessage(const QString &json);
    void onOpenStream(const OpenStreamMsg &os);
    void cleanupStream(quint64 streamId);
    void scheduleReconnect();

    ClientConfig cfg_;
    QWebSocket *ws_ = nullptr;
    QNetworkAccessManager *nam_ = nullptr;
    QTimer reconnect_;
    int backoffSec_ = 1;
    bool running_ = false;
    bool connected_ = false;
    QMap<QString, QString> localMap_;
    QHash<quint64, StreamState> streams_;
    quint64 nextMgmtId_ = 0;
    QHash<quint64, MgmtCallback> mgmtPending_;
};

} // namespace tunnel
