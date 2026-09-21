#include "tunnel/Client.hpp"
#include "tunnel/Forwarder.hpp"

#include <QAbstractSocket>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QUrl>
#include <QWebSocket>

namespace tunnel {

Client::Client(QObject *parent) : QObject(parent) {
    nam_ = new QNetworkAccessManager(this);
    reconnect_.setSingleShot(true);
    connect(&reconnect_, &QTimer::timeout, this, &Client::openConnection);
}

void Client::setConfig(const ClientConfig &cfg) {
    cfg_ = cfg;
    localMap_.clear();
    for (const auto &t : cfg_.tunnels)
        localMap_[t.tunnelId] = t.localAddr;
}

void Client::start() {
    if (running_)
        return;
    running_ = true;
    backoffSec_ = 1;
    openConnection();
}

void Client::stop() {
    running_ = false;
    reconnect_.stop();
    if (ws_)
        ws_->close();
}

void Client::openConnection() {
    if (!running_)
        return;
    if (!ws_) {
        ws_ = new QWebSocket(QString(), QWebSocketProtocol::VersionLatest, this);
        connect(ws_, &QWebSocket::connected, this, [this]() {
            connected_ = true;
            backoffSec_ = 1;
            emit stateChanged(QStringLiteral("connected"));
            const QString host = QUrl(cfg_.serverUrl).host();
            emit log(QStringLiteral("ok"), QStringLiteral("net"),
                     QStringLiteral("控制通道已建立 %1，注册 %2 条隧道")
                         .arg(host)
                         .arg(cfg_.tunnels.size()));
            sendText(buildRegister(cfg_.clientId, cfg_.tunnels));
        });
        connect(ws_, &QWebSocket::disconnected, this, [this]() {
            const bool was = connected_;
            connected_ = false;
            emit stateChanged(QStringLiteral("disconnected"));
            if (was)
                emit log(QStringLiteral("info"), QStringLiteral("net"), QStringLiteral("连接断开"));
            const auto ids = streams_.keys();
            for (quint64 id : ids)
                cleanupStream(id);
            if (running_ && cfg_.autoReconnect)
                scheduleReconnect();
        });
        connect(ws_, &QWebSocket::textMessageReceived, this, &Client::handleMessage);
        connect(ws_, &QWebSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) {
            if (ws_)
                emit log(QStringLiteral("warn"), QStringLiteral("net"),
                         QStringLiteral("连接错误: ") + ws_->errorString());
        });
    }

    QNetworkRequest req{QUrl(cfg_.serverUrl)};
    req.setRawHeader("Authorization", ("Bearer " + cfg_.apiToken).toUtf8());
    emit stateChanged(QStringLiteral("connecting"));
    ws_->open(req);
}

void Client::scheduleReconnect() {
    emit log(QStringLiteral("warn"), QStringLiteral("net"),
             QStringLiteral("%1 秒后重连…").arg(backoffSec_));
    reconnect_.start(backoffSec_ * 1000);
    backoffSec_ = qMin(backoffSec_ * 2, 30);
}

void Client::sendText(const QString &s) {
    if (ws_ && ws_->state() == QAbstractSocket::ConnectedState)
        ws_->sendTextMessage(s);
}

void Client::handleMessage(const QString &json) {
    const ParsedMsg m = parseServerMessage(json);
    switch (m.type) {
    case MsgType::RegisterAck: {
        emit ack(m.ackOk, m.ackError, m.tunnels);
        if (m.ackOk) {
            for (const auto &t : m.tunnels)
                emit log(QStringLiteral("ok"), t.tunnelId,
                         QStringLiteral("已暴露 %1").arg(t.publicUrl));
        } else {
            emit log(QStringLiteral("err"), QStringLiteral("net"),
                     QStringLiteral("注册失败: ") + m.ackError);
        }
        break;
    }
    case MsgType::Ping:
        sendText(buildPong());
        break;
    case MsgType::OpenStream:
        onOpenStream(m.openStream);
        break;
    case MsgType::ServerChunk: {
        auto it = streams_.find(m.streamId);
        if (it != streams_.end() && it->fwd)
            it->fwd->writeFromRemote(m.chunk);
        break;
    }
    case MsgType::CloseStream: {
        auto it = streams_.find(m.streamId);
        if (it != streams_.end() && it->fwd)
            it->fwd->closeFromRemote();
        break;
    }
    default:
        break;
    }
}

void Client::onOpenStream(const OpenStreamMsg &os) {
    const QString local = localMap_.value(os.tunnelId);
    if (local.isEmpty()) {
        sendText(buildAbort(os.streamId, QStringLiteral("unknown tunnel ") + os.tunnelId));
        emit log(QStringLiteral("err"), os.tunnelId, QStringLiteral("收到未知隧道请求"));
        return;
    }
    auto *fwd = new Forwarder(os, local, nam_, this);
    StreamState st;
    st.fwd = fwd;
    st.tunnelId = os.tunnelId;
    st.timer.start();
    streams_.insert(os.streamId, st);

    connect(fwd, &Forwarder::head, this,
            [this](quint64 sid, int status, const QMap<QString, QString> &headers) {
                auto it = streams_.find(sid);
                if (it != streams_.end())
                    it->status = status;
                sendText(buildResponseHead(sid, status, headers));
            });
    connect(fwd, &Forwarder::chunk, this, [this](quint64 sid, const QByteArray &data) {
        auto it = streams_.find(sid);
        if (it != streams_.end())
            it->bytes += data.size();
        sendText(buildChunk(sid, data));
    });
    connect(fwd, &Forwarder::finished, this, [this](quint64 sid) {
        auto it = streams_.find(sid);
        if (it == streams_.end())
            return;
        sendText(buildEnd(sid));
        emit requestFinished(it->tunnelId, it->status, it->bytes, it->timer.elapsed());
        cleanupStream(sid);
    });
    connect(fwd, &Forwarder::failed, this, [this](quint64 sid, const QString &reason) {
        auto it = streams_.find(sid);
        if (it != streams_.end())
            emit log(QStringLiteral("err"), it->tunnelId,
                     QStringLiteral("本地转发失败: ") + reason);
        sendText(buildAbort(sid, reason));
        cleanupStream(sid);
    });

    fwd->start();
}

void Client::cleanupStream(quint64 streamId) {
    auto it = streams_.find(streamId);
    if (it == streams_.end())
        return;
    if (it->fwd) {
        it->fwd->disconnect(this);
        it->fwd->deleteLater();
    }
    streams_.erase(it);
}

} // namespace tunnel
