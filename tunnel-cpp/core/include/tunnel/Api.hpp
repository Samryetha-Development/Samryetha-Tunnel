#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QObject>

#include <functional>

class QNetworkAccessManager;
class QNetworkRequest;

namespace tunnel {

/// 认证与 REST 管理 API（Token / 隧道 / 访客鉴权 / 用量 / 管理员）
class Api : public QObject {
    Q_OBJECT
public:
    /// 回调：ok=是否成功，obj=响应 JSON，error=错误信息
    using Callback = std::function<void(bool, const QJsonObject &, const QString &)>;

    explicit Api(QObject *parent = nullptr);

    /// 通用请求（method: GET/POST/PATCH/DELETE）
    void request(const QString &base, const QString &token, const QString &method,
                 const QString &path, const QJsonObject &body, Callback cb);

    // ---- 兼容旧接口 ----
    void getConfig(const QString &httpBase);
    void deviceStart(const QString &httpBase);
    void devicePoll(const QString &httpBase, const QString &deviceCode);
    void devToken(const QString &httpBase, const QString &email);
    void listTunnels(const QString &httpBase, const QString &token);

signals:
    void configReceived(const QJsonObject &config);
    void deviceCodeReceived(const QString &userCode, const QString &verificationUri,
                            const QString &deviceCode, int interval);
    void tokenReceived(const QString &token, const QJsonObject &user);
    void tunnelsReceived(const QJsonArray &tunnels);
    void failed(const QString &error);

private:
    QNetworkAccessManager *nam_;
};

} // namespace tunnel
