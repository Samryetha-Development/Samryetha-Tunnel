#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QObject>

#include <functional>

class QNetworkAccessManager;
class QNetworkRequest;

namespace tunnel {

/// 认证与 REST API：设备码登录（SSO）、dev 登录、隧道/令牌管理
class Api : public QObject {
    Q_OBJECT
public:
    explicit Api(QObject *parent = nullptr);

    /// GET /auth/config
    void getConfig(const QString &httpBase);
    /// POST /auth/device/start
    void deviceStart(const QString &httpBase);
    /// POST /auth/device/poll
    void devicePoll(const QString &httpBase, const QString &deviceCode);
    /// POST /auth/dev-token（AUTH_MODE=dev 时可用，方便本地联调）
    void devToken(const QString &httpBase, const QString &email);
    /// GET /api/tunnels
    void listTunnels(const QString &httpBase, const QString &token);

signals:
    void configReceived(const QJsonObject &config);
    void deviceCodeReceived(const QString &userCode, const QString &verificationUri,
                            const QString &deviceCode, int interval);
    void tokenReceived(const QString &token, const QJsonObject &user);
    void tunnelsReceived(const QJsonArray &tunnels);
    void failed(const QString &error);

private:
    QNetworkRequest makeRequest(const QString &url, const QString &token = QString());
    void postJson(const QString &url, const QJsonObject &body, const QString &token,
                  std::function<void(const QJsonObject &)> ok,
                  std::function<void(const QString &)> err);
    QNetworkAccessManager *nam_;
};

} // namespace tunnel
