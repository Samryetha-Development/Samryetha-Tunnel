// 跨平台 CLI 客户端：一套代码在 macOS / Windows 编译
// 用法示例：
//   tunnel-cli --server ws://127.0.0.1:18090/tunnel --dev-token alice@test.dev \
//              --tunnel id=web,path=/web,local=127.0.0.1:8080
//   tunnel-cli --config client.json --device-login

#include "tunnel/Api.hpp"
#include "tunnel/Client.hpp"
#include "tunnel/Config.hpp"

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>
#include <QTimer>

#include <atomic>
#include <csignal>

using namespace tunnel;

static std::atomic<bool> g_quit{false};

static QString ts() {
    return QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss"));
}

static void print(const QString &level, const QString &source, const QString &message) {
    QTextStream out(stdout);
    out << QStringLiteral("[%1][%2][%3] %4\n").arg(ts(), level, source, message);
    out.flush();
}

static TunnelDef parseTunnelSpec(const QString &spec, bool *ok) {
    TunnelDef d;
    *ok = false;
    for (const QString &part : spec.split(',', Qt::SkipEmptyParts)) {
        const int eq = part.indexOf('=');
        if (eq <= 0)
            continue;
        const QString k = part.left(eq).trimmed().toLower();
        const QString v = part.mid(eq + 1).trimmed();
        if (k == "id" || k == "tunnel_id")
            d.tunnelId = v;
        else if (k == "proto")
            d.proto = v;
        else if (k == "sub" || k == "subdomain")
            d.subdomain = v;
        else if (k == "path" || k == "prefix" || k == "path_prefix")
            d.pathPrefix = v;
        else if (k == "local" || k == "local_addr")
            d.localAddr = v;
    }
    *ok = !d.tunnelId.isEmpty() && !d.localAddr.isEmpty();
    return d;
}

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("tunnel-cli"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.3.0"));

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("Samryetha Tunnel CLI（跨平台 C++ 客户端）"));
    parser.addHelpOption();
    parser.addVersionOption();

    const QCommandLineOption optServer({"s", "server"}, "控制通道地址，如 wss://frp.x.com/tunnel", "url");
    const QCommandLineOption optToken({"t", "token"}, "API Token", "token");
    const QCommandLineOption optClientId("client-id", "客户端 ID", "id");
    const QCommandLineOption optBase("base-domain", "主域名（仅展示用）", "domain");
    const QCommandLineOption optConfig({"c", "config"}, "配置文件 JSON", "file");
    const QCommandLineOption optTunnel("tunnel", "隧道定义 id=..,proto=http|tcp,sub=..,path=..,local=host:port（可重复）", "spec");
    const QCommandLineOption optDevToken("dev-token", "用 dev 模式邮箱换取 Token 后连接", "email");
    const QCommandLineOption optDevice("device-login", "用 SSO 设备码登录");
    const QCommandLineOption optNoReconnect("no-reconnect", "关闭断线自动重连");
    const QCommandLineOption optSave("save-config", "把当前配置保存到文件后退出", "file");
    const QCommandLineOption optMgmt("mgmt", "连接后执行管理请求(GET)并退出", "path");
    const QCommandLineOption optMgmtPost("mgmt-post", "连接后执行管理请求: PATH JSON", "spec");

    parser.addOptions({optServer, optToken, optClientId, optBase, optConfig, optTunnel,
                       optDevToken, optDevice, optNoReconnect, optSave, optMgmt, optMgmtPost});
    parser.process(app);

    ClientConfig cfg;
    if (parser.isSet(optConfig)) {
        cfg = ClientConfig::load(parser.value(optConfig));
        if (cfg.tunnels.isEmpty() && cfg.serverUrl.isEmpty()) {
            print("warn", "cli", "配置文件为空或不存在，使用默认值");
        }
    }
    if (parser.isSet(optServer))
        cfg.serverUrl = parser.value(optServer);
    if (parser.isSet(optToken))
        cfg.apiToken = parser.value(optToken);
    if (parser.isSet(optClientId))
        cfg.clientId = parser.value(optClientId);
    if (parser.isSet(optBase))
        cfg.baseDomain = parser.value(optBase);
    if (parser.isSet(optNoReconnect))
        cfg.autoReconnect = false;

    if (parser.isSet(optTunnel)) {
        cfg.tunnels.clear();
        for (const QString &spec : parser.values(optTunnel)) {
            bool ok = false;
            const TunnelDef d = parseTunnelSpec(spec, &ok);
            if (!ok) {
                print("err", "cli", QStringLiteral("隧道参数非法: ") + spec);
                return 2;
            }
            cfg.tunnels.append(d);
        }
    }

    if (cfg.serverUrl.isEmpty()) {
        print("err", "cli", "缺少 --server");
        return 2;
    }
    if (cfg.tunnels.isEmpty()) {
        print("warn", "cli", "没有配置任何隧道，连接后不会暴露服务");
    }

    const QString httpBase = ClientConfig::httpBaseFromWs(cfg.serverUrl);

    if (parser.isSet(optSave)) {
        if (cfg.save(parser.value(optSave)))
            print("ok", "cli", QStringLiteral("配置已保存到 ") + parser.value(optSave));
        else
            print("err", "cli", "保存失败");
        return 0;
    }

    Client client;
    client.setConfig(cfg);
    QObject::connect(&client, &Client::log, &app, &print);
    QObject::connect(&client, &Client::stateChanged, &app, [](const QString &s) {
        print("info", "state", s);
    });
    QObject::connect(&client, &Client::ack, &app,
                     [](bool ok, const QString &err, const QList<EffectiveTunnel> &list) {
                         if (!err.isEmpty())
                             print("warn", "net", QStringLiteral("服务端提示: ") + err);
                         if (!ok)
                             print("err", "net", QStringLiteral("注册失败"));
                         for (const auto &t : list)
                             print("ok", t.tunnelId, QStringLiteral("公网地址 %1").arg(t.publicUrl));
                     });
    // 管理请求模式：连接成功后发一条 mgmt，打印结果后退出
    if (parser.isSet(optMgmt) || parser.isSet(optMgmtPost)) {
        const bool post = parser.isSet(optMgmtPost);
        QString mpath, mjson;
        if (post) {
            const QString spec = parser.value(optMgmtPost);
            const int sp = spec.indexOf(' ');
            mpath = sp < 0 ? spec : spec.left(sp);
            mjson = sp < 0 ? QStringLiteral("{}") : spec.mid(sp + 1);
        } else {
            mpath = parser.value(optMgmt);
        }
        QObject::connect(&client, &Client::stateChanged, &app, [&client, mpath, mjson, post, &app](const QString &st) {
            if (st != QLatin1String("connected"))
                return;
            const QJsonObject body = post ? QJsonDocument::fromJson(mjson.toUtf8()).object() : QJsonObject();
            client.mgmtRequest(post ? "POST" : "GET", mpath, body,
                               [&app](bool ok, const QJsonObject &o, const QString &e) {
                                   if (ok)
                                       print("ok", "mgmt", QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact)));
                                   else
                                       print("err", "mgmt", e);
                                   QCoreApplication::exit(ok ? 0 : 1);
                               });
        });
    }

    QObject::connect(&client, &Client::requestFinished, &app,
                     [](const QString &id, int status, qint64 bytes, int ms) {
                         print("info", id,
                               QStringLiteral("%1 · %2B · %3ms").arg(status).arg(bytes).arg(ms));
                     });

    Api api;
    QObject::connect(&api, &Api::failed, &app, [&app](const QString &e) {
        print("err", "auth", e);
        QCoreApplication::exit(3);
    });

    QTimer poller;
    QString deviceCode;
    int deviceInterval = 5;
    QObject::connect(&poller, &QTimer::timeout, &app, [&]() {
        if (!deviceCode.isEmpty())
            api.devicePoll(httpBase, deviceCode);
    });

    QObject::connect(&api, &Api::deviceCodeReceived, &app,
                     [&](const QString &userCode, const QString &uri, const QString &code, int interval) {
                         deviceCode = code;
                         deviceInterval = interval > 0 ? interval : 5;
                         print("ok", "auth",
                               QStringLiteral("请在浏览器打开 %1 并输入代码 %2").arg(uri, userCode));
                         poller.start(deviceInterval * 1000);
                     });

    QObject::connect(&api, &Api::tokenReceived, &app,
                     [&](const QString &token, const QJsonObject &user) {
                         poller.stop();
                         cfg.apiToken = token;
                         client.setConfig(cfg);
                         print("ok", "auth",
                               QStringLiteral("登录成功: %1").arg(user.value("email").toString()));
                         client.start();
                     });

    if (parser.isSet(optDevToken)) {
        print("info", "auth", QStringLiteral("dev 登录换取 Token…"));
        api.devToken(httpBase, parser.value(optDevToken));
    } else if (parser.isSet(optDevice)) {
        print("info", "auth", QStringLiteral("请求设备码…"));
        api.deviceStart(httpBase);
    } else {
        if (cfg.apiToken.isEmpty()) {
            print("err", "cli", "缺少 --token（或用 --dev-token / --device-login）");
            return 2;
        }
        client.start();
    }

    QTimer sigTimer;
    QObject::connect(&sigTimer, &QTimer::timeout, &app, []() {
        if (g_quit.load()) {
            print("info", "cli", "退出中…");
            QCoreApplication::quit();
        }
    });
    sigTimer.start(200);
    std::signal(SIGINT, [](int) { g_quit.store(true); });
    std::signal(SIGTERM, [](int) { g_quit.store(true); });

    return app.exec();
}
