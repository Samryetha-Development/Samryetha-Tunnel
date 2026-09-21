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

QNetworkRequest Api::makeRequest(const QString &url, const QString &token) {
    QNetworkRequest req{QUrl(url)};
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    if (!token.isEmpty())
        req.setRawHeader("Authorization", ("Bearer " + token).toUtf8());
    return req;
}

void Api::postJson(const QString &url, const QJsonObject &body, const QString &token,
                   std::function<void(const QJsonObject &)> ok,
                   std::function<void(const QString &)> err) {
    QNetworkRequest req = makeRequest(url, token);
    QNetworkReply *reply =
        nam_->post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [reply, ok, err, this]() {
        const QByteArray data = reply->readAll();
        const int httpStatus =
            reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        reply->deleteLater();
        const auto doc = QJsonDocument::fromJson(data);
        const QJsonObject obj = doc.isObject() ? doc.object() : QJsonObject();
        if (reply->error() != QNetworkReply::NoError && httpStatus == 0) {
            err(reply->errorString());
            return;
        }
        if (!obj.isEmpty() && obj.contains("error") && httpStatus >= 400) {
            err(obj.value("error").toString());
            return;
        }
        ok(obj);
    });
}

void Api::getConfig(const QString &httpBase) {
    QNetworkReply *reply = nam_->get(makeRequest(httpBase + "/auth/config"));
    connect(reply, &QNetworkReply::finished, this, [reply, this]() {
        const auto doc = QJsonDocument::fromJson(reply->readAll());
        reply->deleteLater();
        if (doc.isObject())
            emit configReceived(doc.object());
        else
            emit failed(QStringLiteral("无法获取 /auth/config"));
    });
}

void Api::deviceStart(const QString &httpBase) {
    postJson(httpBase + "/auth/device/start", {},
             {}, [this](const QJsonObject &o) {
                 emit deviceCodeReceived(o.value("user_code").toString(),
                                         o.value("verification_uri").toString(),
                                         o.value("device_code").toString(),
                                         o.value("interval").toInt(5));
             },
             [this](const QString &e) { emit failed(e); });
}

void Api::devicePoll(const QString &httpBase, const QString &deviceCode) {
    QJsonObject body;
    body["device_code"] = deviceCode;
    postJson(httpBase + "/auth/device/poll", body, {},
             [this](const QJsonObject &o) {
                 const QString status = o.value("status").toString();
                 if (status == "ok")
                     emit tokenReceived(o.value("token").toString(), o.value("user").toObject());
                 else if (status == "error")
                     emit failed(o.value("error").toString() + " " +
                                 o.value("description").toString());
             },
             [this](const QString &e) { emit failed(e); });
}

void Api::devToken(const QString &httpBase, const QString &email) {
    QJsonObject body;
    body["email"] = email;
    postJson(httpBase + "/auth/dev-token", body, {},
             [this](const QJsonObject &o) {
                 emit tokenReceived(o.value("token").toString(), o.value("user").toObject());
             },
             [this](const QString &e) { emit failed(e); });
}

void Api::listTunnels(const QString &httpBase, const QString &token) {
    QNetworkReply *reply = nam_->get(makeRequest(httpBase + "/api/tunnels", token));
    connect(reply, &QNetworkReply::finished, this, [reply, this]() {
        const auto doc = QJsonDocument::fromJson(reply->readAll());
        reply->deleteLater();
        if (doc.isObject())
            emit tunnelsReceived(doc.object().value("tunnels").toArray());
        else
            emit failed(QStringLiteral("无法获取隧道列表"));
    });
}

} // namespace tunnel
