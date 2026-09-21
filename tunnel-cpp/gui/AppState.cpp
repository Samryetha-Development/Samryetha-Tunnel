#include "AppState.hpp"

#include <QDir>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QStandardPaths>
#include <QUrl>

using namespace tunnel;

namespace gui {

AppState::AppState(QObject *parent) : QObject(parent) {
    client_ = new Client(this);
    connect(client_, &Client::log, this, [this](const QString &l, const QString &s, const QString &m) {
        logs_.append(LogEntry{QDateTime::currentDateTime(), l, s, m});
        if (logs_.size() > 2000)
            logs_.remove(0, logs_.size() - 2000);
        emit logsChanged();
    });
    connect(client_, &Client::stateChanged, this, [this](const QString &s) {
        if (s == QLatin1String("connected"))
            connectedAt_ = QDateTime::currentDateTime();
        if (s == QLatin1String("disconnected"))
            connectedAt_ = QDateTime();
        emit stateChanged();
    });
    connect(client_, &Client::ack, this,
            [this](bool ok, const QString &err, const QList<EffectiveTunnel> &tunnels) {
                effective_ = tunnels;
                if (!ok)
                    lastError_ = err;
                else
                    lastError_.clear();
                emit stateChanged();
            });
    connect(client_, &Client::requestFinished, this,
            [this](const QString &id, int status, qint64 bytes, int ms) {
                RequestEvent e{QDateTime::currentDateTime(), id, status, bytes, ms};
                events_.append(e);
                if (events_.size() > 3000)
                    events_.remove(0, events_.size() - 3000);
                auto &st = stats_[id];
                st.count += 1;
                st.bytes += bytes;
                if (status >= 500)
                    st.errCount += 1;
                st.lastStatus = status;
                st.lastMs = ms;
                st.lastAt = e.at;
                emit statsChanged();
            });
}

QString AppState::configPath() const {
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QDir().mkpath(dir);
    return dir + "/client.json";
}

void AppState::load() {
    cfg_ = ClientConfig::load(configPath());
    client_->setConfig(cfg_);
    emit tunnelsChanged();
    emit stateChanged();
}

void AppState::save() {
    client_->setConfig(cfg_);
    cfg_.save(configPath());
    emit tunnelsChanged();
}

void AppState::setApiToken(const QString &token) {
    cfg_.apiToken = token;
    save();
}

void AppState::connectServer() {
    if (cfg_.serverUrl.isEmpty()) {
        lastError_ = QStringLiteral("服务端地址为空");
        emit stateChanged();
        return;
    }
    client_->setConfig(cfg_);
    client_->start();
    emit stateChanged();
}

void AppState::disconnectServer() {
    client_->stop();
    connectedAt_ = QDateTime();
    emit stateChanged();
}

void AppState::testLocal(const QString &tunnelId, const QString &localAddr) {
    auto *nam = new QNetworkAccessManager(this);
    QNetworkRequest req{QUrl(QStringLiteral("http://") + localAddr + "/")};
    QNetworkReply *reply = nam->get(req);
    const qint64 t0 = QDateTime::currentMSecsSinceEpoch();
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, nam, tunnelId, t0]() {
                const qint64 ms = QDateTime::currentMSecsSinceEpoch() - t0;
                const int code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
                QString msg;
                if (reply->error() != QNetworkReply::NoError && code == 0)
                    msg = QStringLiteral("不通: ") + reply->errorString();
                else
                    msg = QStringLiteral("本地连通 %1 · %2ms").arg(code).arg(ms);
                logs_.append(LogEntry{QDateTime::currentDateTime(),
                                      msg.startsWith(QStringLiteral("不通")) ? "err" : "ok",
                                      tunnelId, msg});
                emit logsChanged();
                reply->deleteLater();
                nam->deleteLater();
            });
}

void AppState::addTunnel(const TunnelDef &d) {
    cfg_.tunnels.append(d);
    save();
}

void AppState::updateTunnel(const QString &originalId, const TunnelDef &d) {
    for (int i = 0; i < cfg_.tunnels.size(); ++i) {
        if (cfg_.tunnels[i].tunnelId == originalId) {
            cfg_.tunnels[i] = d;
            break;
        }
    }
    save();
}

void AppState::removeTunnel(const QString &tunnelId) {
    for (int i = 0; i < cfg_.tunnels.size(); ++i) {
        if (cfg_.tunnels[i].tunnelId == tunnelId) {
            cfg_.tunnels.removeAt(i);
            break;
        }
    }
    stats_.remove(tunnelId);
    save();
    emit statsChanged();
}

void AppState::clearLogs() {
    logs_.clear();
    emit logsChanged();
}

void AppState::clearStats() {
    events_.clear();
    stats_.clear();
    emit statsChanged();
}

QString AppState::statusText() const {
    if (client_->isConnected())
        return QStringLiteral("已连接");
    return QStringLiteral("未连接");
}

qint64 AppState::uptimeSecs() const {
    if (!connectedAt_.isValid())
        return 0;
    return connectedAt_.secsTo(QDateTime::currentDateTime());
}

} // namespace gui
