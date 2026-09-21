#pragma once

#include "tunnel/Protocol.hpp"

#include <QByteArray>
#include <QObject>

class QNetworkAccessManager;
class QNetworkReply;
class QTcpSocket;

namespace tunnel {

/// 把一条隧道流转发到本地服务：
/// - http：用 QNetworkAccessManager 请求本地，把响应流式回传（SSE 友好）
/// - tcp ：用 QTcpSocket 与本地双向对接
class Forwarder : public QObject {
    Q_OBJECT
public:
    Forwarder(const OpenStreamMsg &req, const QString &localAddr, QNetworkAccessManager *nam,
              QObject *parent = nullptr);
    ~Forwarder() override;

    void start();
    /// TCP：服务端上行数据 -> 本地
    void writeFromRemote(const QByteArray &data);
    /// 访客断开：关闭本地连接
    void closeFromRemote();
    quint64 streamId() const { return req_.streamId; }
    QString tunnelId() const { return req_.tunnelId; }

signals:
    void head(quint64 streamId, int status, const QMap<QString, QString> &headers);
    void chunk(quint64 streamId, const QByteArray &data);
    void finished(quint64 streamId);
    void failed(quint64 streamId, const QString &reason);

private:
    void startHttp();
    void startTcp();
    void finishOnce();

    OpenStreamMsg req_;
    QString localAddr_;
    QNetworkAccessManager *nam_;
    QNetworkReply *reply_ = nullptr;
    QTcpSocket *socket_ = nullptr;
    QByteArray pendingRemote_; // TCP：本地连接建立前先到的上行数据
    bool headersSent_ = false;
    bool ended_ = false;
};

} // namespace tunnel
