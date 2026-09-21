#include "tunnel/Forwarder.hpp"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTcpSocket>
#include <QUrl>

namespace tunnel {

Forwarder::Forwarder(const OpenStreamMsg &req, const QString &localAddr, QNetworkAccessManager *nam,
                     QObject *parent)
    : QObject(parent), req_(req), localAddr_(localAddr), nam_(nam) {}

Forwarder::~Forwarder() {
    if (reply_) {
        reply_->disconnect(this);
        reply_->deleteLater();
    }
    if (socket_) {
        socket_->disconnect(this);
        socket_->deleteLater();
    }
}

void Forwarder::start() {
    if (req_.proto == "tcp")
        startTcp();
    else
        startHttp();
}

void Forwarder::startHttp() {
    const QString url = QStringLiteral("http://") + localAddr_ + (req_.path.isEmpty() ? "/" : req_.path);
    QNetworkRequest request{QUrl(url)};
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    request.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
    request.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);
    for (auto it = req_.headers.constBegin(); it != req_.headers.constEnd(); ++it) {
        const QString k = it.key().toLower();
        if (k == "host" || k == "content-length" || k == "connection" || k == "accept-encoding")
            continue;
        request.setRawHeader(it.key().toUtf8(), it.value().toUtf8());
    }

    reply_ = nam_->sendCustomRequest(request, req_.method.isEmpty() ? "GET" : req_.method.toUtf8(),
                                     req_.body);

    connect(reply_, &QNetworkReply::metaDataChanged, this, [this]() {
        if (headersSent_ || !reply_)
            return;
        const QVariant st = reply_->attribute(QNetworkRequest::HttpStatusCodeAttribute);
        const int status = st.isValid() ? st.toInt() : 200;
        QMap<QString, QString> h;
        for (const auto &pair : reply_->rawHeaderPairs()) {
            const QString k = QString::fromUtf8(pair.first).toLower();
            if (k == "content-length" || k == "connection" || k == "transfer-encoding")
                continue;
            h[QString::fromUtf8(pair.first)] = QString::fromUtf8(pair.second);
        }
        headersSent_ = true;
        emit head(req_.streamId, status, h);
    });

    connect(reply_, &QNetworkReply::readyRead, this, [this]() {
        if (!reply_)
            return;
        const QByteArray data = reply_->readAll();
        if (!data.isEmpty())
            emit chunk(req_.streamId, data);
    });

    connect(reply_, &QNetworkReply::finished, this, [this]() {
        if (!reply_)
            return;
        // 若还没发过头（例如本地服务直接失败），补一个 502
        if (!headersSent_) {
            const QVariant st = reply_->attribute(QNetworkRequest::HttpStatusCodeAttribute);
            if (!st.isValid() && reply_->error() != QNetworkReply::NoError) {
                const QString reason = reply_->errorString();
                emit failed(req_.streamId, reason);
                finishOnce();
                return;
            }
            const int status = st.isValid() ? st.toInt() : 200;
            QMap<QString, QString> h;
            for (const auto &pair : reply_->rawHeaderPairs()) {
                const QString k = QString::fromUtf8(pair.first).toLower();
                if (k == "content-length" || k == "connection" || k == "transfer-encoding")
                    continue;
                h[QString::fromUtf8(pair.first)] = QString::fromUtf8(pair.second);
            }
            headersSent_ = true;
            emit head(req_.streamId, status, h);
        }
        const QByteArray tail = reply_->readAll();
        if (!tail.isEmpty())
            emit chunk(req_.streamId, tail);
        finishOnce();
    });
}

void Forwarder::startTcp() {
    const int colon = localAddr_.lastIndexOf(':');
    if (colon <= 0) {
        emit failed(req_.streamId, QStringLiteral("本地地址非法: ") + localAddr_);
        return;
    }
    const QString host = localAddr_.left(colon);
    const quint16 port = static_cast<quint16>(localAddr_.mid(colon + 1).toUShort());

    socket_ = new QTcpSocket(this);
    connect(socket_, &QTcpSocket::connected, this, [this]() {
        if (!pendingRemote_.isEmpty()) {
            socket_->write(pendingRemote_);
            pendingRemote_.clear();
        }
    });
    connect(socket_, &QTcpSocket::readyRead, this, [this]() {
        const QByteArray data = socket_->readAll();
        if (!data.isEmpty())
            emit chunk(req_.streamId, data);
    });
    connect(socket_, &QTcpSocket::disconnected, this, [this]() { finishOnce(); });
    connect(socket_, &QTcpSocket::errorOccurred, this, [this](QAbstractSocket::SocketError e) {
        // 远端正常关闭不算失败
        if (e == QAbstractSocket::RemoteHostClosedError) {
            finishOnce();
            return;
        }
        if (!ended_)
            emit failed(req_.streamId, socket_->errorString());
        finishOnce();
    });
    socket_->connectToHost(host, port);
}

void Forwarder::writeFromRemote(const QByteArray &data) {
    if (!socket_)
        return;
    if (socket_->state() == QAbstractSocket::ConnectedState)
        socket_->write(data);
    else
        pendingRemote_.append(data); // 连接建立前的早到数据先缓存
}

void Forwarder::closeFromRemote() {
    if (socket_) {
        socket_->disconnectFromHost();
    } else if (reply_) {
        reply_->abort();
        finishOnce();
    }
}

void Forwarder::finishOnce() {
    if (ended_)
        return;
    ended_ = true;
    emit finished(req_.streamId);
}

} // namespace tunnel
