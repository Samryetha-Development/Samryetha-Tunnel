#include "tunnel/Api.hpp"

#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

namespace tunnel {

Api::Api(QObject *parent) : QObject(parent) {
    nam_ = new QNetworkAccessManager(this);
}

void Api::request(const QString &base, const QString &token, const QString &method,
                  const QString &path, const QJsonObject &body, Callback cb) {
    QNetworkRequest req{QUrl(base + path)};
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    if (!token.isEmpty())
        req.setRawHeader("Authorization", ("Bearer " + token).toUtf8());

    const QByteArray payload =
        body.isEmpty() ? QByteArray() : QJsonDocument(body).toJson(QJsonDocument::Compact);

    QNetworkReply *reply = nullptr;
    const QString m = method.toUpper();
    if (m == "GET")
        reply = nam_->get(req);
    else if (m == "POST")
        reply = nam_->post(req, payload);
    else if (m == "DELETE")
        reply = nam_->sendCustomRequest(req, "DELETE", payload);
    else
        reply = nam_->sendCustomRequest(req, m.toUtf8(), payload);

    connect(reply, &QNetworkReply::finished, this, [reply, cb]() {
        const QByteArray data = reply->readAll();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QString netErr = reply->errorString();
        const bool netFail = (reply->error() != QNetworkReply::NoError) && status == 0;
        reply->deleteLater();

        const auto doc = QJsonDocument::fromJson(data);
        QJsonObject obj = doc.isObject() ? doc.object() : QJsonObject();
        if (netFail) {
            cb(false, obj, netErr);
            return;
        }
        if (status >= 400) {
            const QString msg = obj.value("error").toString(
                QStringLiteral("HTTP %1").arg(status));
            cb(false, obj, msg);
            return;
        }
        cb(true, obj, QString());
    });
}

// ---------------- 兼容旧接口 ----------------

void Api::getConfig(const QString &httpBase) {
    request(httpBase, QString(), "GET", "/auth/config", {},
            [this](bool ok, const QJsonObject &o, const QString &e) {
                if (ok)
                    emit configReceived(o);
                else
                    emit failed(e);
            });
}

void Api::deviceStart(const QString &httpBase) {
    request(httpBase, QString(), "POST", "/auth/device/start", {},
            [this](bool ok, const QJsonObject &o, const QString &e) {
                if (!ok) {
                    emit failed(e);
                    return;
                }
                emit deviceCodeReceived(o.value("user_code").toString(),
                                        o.value("verification_uri").toString(),
                                        o.value("device_code").toString(),
                                        o.value("interval").toInt(5));
            });
}

void Api::devicePoll(const QString &httpBase, const QString &deviceCode) {
    QJsonObject body;
    body["device_code"] = deviceCode;
    request(httpBase, QString(), "POST", "/auth/device/poll", body,
            [this](bool ok, const QJsonObject &o, const QString &e) {
                if (!ok) {
                    emit failed(e);
                    return;
                }
                const QString st = o.value("status").toString();
                if (st == "ok")
                    emit tokenReceived(o.value("token").toString(), o.value("user").toObject());
                else if (st == "error")
                    emit failed(o.value("error").toString() + " " +
                                o.value("description").toString());
            });
}

void Api::devToken(const QString &httpBase, const QString &email) {
    QJsonObject body;
    body["email"] = email;
    request(httpBase, QString(), "POST", "/auth/dev-token", body,
            [this](bool ok, const QJsonObject &o, const QString &e) {
                if (ok)
                    emit tokenReceived(o.value("token").toString(), o.value("user").toObject());
                else
                    emit failed(e);
            });
}

void Api::listTunnels(const QString &httpBase, const QString &token) {
    request(httpBase, token, "GET", "/api/tunnels", {},
            [this](bool ok, const QJsonObject &o, const QString &e) {
                if (ok)
                    emit tunnelsReceived(o.value("tunnels").toArray());
                else
                    emit failed(e);
            });
}

} // namespace tunnel
