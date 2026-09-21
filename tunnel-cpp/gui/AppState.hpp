#pragma once

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
    tunnel::ClientConfig &config() { return cfg_; }
    const QString &lastError() const { return lastError_; }
    QString statusText() const;
    qint64 uptimeSecs() const;

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

private:
    tunnel::Client *client_;
    tunnel::ClientConfig cfg_;
    QVector<LogEntry> logs_;
    QVector<RequestEvent> events_;
    QMap<QString, TunnelStat> stats_;
    QList<tunnel::EffectiveTunnel> effective_;
    QString lastError_;
    QDateTime connectedAt_;
};

} // namespace gui
