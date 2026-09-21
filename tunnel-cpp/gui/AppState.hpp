#pragma once

#include "tunnel/Api.hpp"
#include "tunnel/Client.hpp"
#include "tunnel/Config.hpp"

#include <QDateTime>
#include <QObject>
#include <QVector>

namespace gui {

struct LogEntry {
    QDateTime at;
    QString level;
    QString source;
    QString message;
};

struct RequestEvent {
    QDateTime at;
    QString tunnelId;
    int status = 0;
    qint64 bytes = 0;
    int ms = 0;
};

struct TunnelStat {
    int count = 0;
    qint64 bytes = 0;
    int errCount = 0;
    int lastStatus = 0;
    int lastMs = 0;
    QDateTime lastAt;
};

/// GUI 与 CLI 共用的核心之上，做配置持久化、日志与统计聚合
class AppState : public QObject {
    Q_OBJECT
public:
    explicit AppState(QObject *parent = nullptr);

    tunnel::Client *client() { return client_; }
    tunnel::Api *api() { return api_; }
    tunnel::ClientConfig &config() { return cfg_; }
    const QString &lastError() const { return lastError_; }
    QString statusText() const;
    qint64 uptimeSecs() const;

    /// 当前登录用户信息（来自 /auth/me）
    const QString &role() const { return role_; }
    const QString &email() const { return email_; }
    bool isAdmin() const { return role_ == QStringLiteral("admin"); }
    /// 拉取 /auth/me，成功后发射 meChanged()
    void refreshMe();
    /// 追加一条 GUI 日志
    void addLog(const QString &level, const QString &source, const QString &message);
    /// 管理请求：优先走控制通道（客户端即管理端），未连接时回退到 HTTP API
    void mgmt(const QString &method, const QString &path, const QJsonObject &body,
              tunnel::Client::MgmtCallback cb);

    const QVector<LogEntry> &logs() const { return logs_; }
    const QVector<RequestEvent> &events() const { return events_; }
    const QMap<QString, TunnelStat> &stats() const { return stats_; }
    const QList<tunnel::EffectiveTunnel> &effective() const { return effective_; }

    QString configPath() const;

public slots:
    void load();
    void save();
    void connectServer();
    void disconnectServer();
    void testLocal(const QString &tunnelId, const QString &localAddr);
    void addTunnel(const tunnel::TunnelDef &d);
    void updateTunnel(const QString &originalId, const tunnel::TunnelDef &d);
    void removeTunnel(const QString &tunnelId);
    void clearLogs();
    void clearStats();
    void setApiToken(const QString &token);

signals:
    void logsChanged();
    void statsChanged();
    void stateChanged();
    void tunnelsChanged();
    void meChanged();

private:
    tunnel::Client *client_;
    tunnel::Api *api_;
    QString role_;
    QString email_;
    tunnel::ClientConfig cfg_;
    QVector<LogEntry> logs_;
    QVector<RequestEvent> events_;
    QMap<QString, TunnelStat> stats_;
    QList<tunnel::EffectiveTunnel> effective_;
    QString lastError_;
    QDateTime connectedAt_;
};

} // namespace gui
