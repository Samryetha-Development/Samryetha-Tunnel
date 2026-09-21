#include "SettingsPage.hpp"
#include "AppState.hpp"
#include "TunnelEditorDialog.hpp"
#include "Ui.hpp"

#include "tunnel/Api.hpp"
#include "tunnel/Config.hpp"

#include <QCheckBox>
#include <QDesktopServices>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

using namespace tunnel;

namespace gui {

SettingsPage::SettingsPage(AppState *st, QWidget *parent) : QWidget(parent), st_(st) {
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(34, 30, 34, 24);
    root->setSpacing(0);

    root->addWidget(ui::eyebrow(QStringLiteral("settings")));
    root->addWidget(ui::pageTitle(QStringLiteral("设置")));
    root->addSpacing(6);

    // 服务端
    auto *serverBox = new QGroupBox(QStringLiteral("SERVER"));
    auto *form = new QFormLayout(serverBox);
    server_ = new QLineEdit;
    server_->setPlaceholderText(QStringLiteral("wss://frp.example.com/tunnel"));
    token_ = new QLineEdit;
    token_->setEchoMode(QLineEdit::Password);
    token_->setPlaceholderText(QStringLiteral("API Token"));
    clientId_ = new QLineEdit;
    base_ = new QLineEdit;
    base_->setPlaceholderText(QStringLiteral("frp.example.com"));
    autoReconnect_ = new QCheckBox(QStringLiteral("断线自动重连（指数退避）"));
    form->addRow(QStringLiteral("控制通道地址"), server_);
    form->addRow(QStringLiteral("API Token"), token_);
    form->addRow(QStringLiteral("客户端 ID"), clientId_);
    form->addRow(QStringLiteral("主域名"), base_);
    form->addRow(QString(), autoReconnect_);

    auto *authRow = new QHBoxLayout;
    auto *deviceBtn = new QPushButton(QStringLiteral("SSO 设备码登录"));
    authStatus_ = new QLabel;
    authStatus_->setObjectName("muted");
    authRow->addWidget(deviceBtn);
    authRow->addWidget(authStatus_, 1);
    form->addRow(QStringLiteral("登录"), authRow);
    root->addWidget(serverBox);

    // 隧道
    auto *tunnelBox = new QGroupBox(QStringLiteral("TUNNELS（保存后需重连生效）"));
    auto *tv = new QVBoxLayout(tunnelBox);
    tunnelTable_ = new QTableWidget;
    tunnelTable_->setColumnCount(5);
    tunnelTable_->setHorizontalHeaderLabels({QStringLiteral("ID"), QStringLiteral("类型"),
                                             QStringLiteral("子域名"), QStringLiteral("子路由"),
                                             QStringLiteral("本地")});
    tunnelTable_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    tunnelTable_->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);
    tunnelTable_->verticalHeader()->setVisible(false);
    tunnelTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    tv->addWidget(tunnelTable_, 1);
    auto *btnRow = new QHBoxLayout;
    auto *addBtn = new QPushButton(QStringLiteral("＋ 添加"));
    auto *editBtn = new QPushButton(QStringLiteral("编辑"));
    auto *delBtn = new QPushButton(QStringLiteral("删除"));
    delBtn->setObjectName("danger");
    btnRow->addWidget(addBtn);
    btnRow->addWidget(editBtn);
    btnRow->addWidget(delBtn);
    btnRow->addStretch();
    auto *saveBtn = new QPushButton(QStringLiteral("保存配置"));
    saveBtn->setObjectName("primary");
    btnRow->addWidget(saveBtn);
    tv->addLayout(btnRow);
    root->addWidget(tunnelBox, 1);

    api_ = new Api(this);
    poll_ = new QTimer(this);
    connect(poll_, &QTimer::timeout, this, [this]() {
        if (!deviceCode_.isEmpty())
            api_->devicePoll(ClientConfig::httpBaseFromWs(server_->text().trimmed()), deviceCode_);
    });
    connect(api_, &Api::deviceCodeReceived, this,
            [this](const QString &userCode, const QString &uri, const QString &code, int interval) {
                deviceCode_ = code;
                authStatus_->setText(QStringLiteral("请在 %1 输入代码 %2").arg(uri, userCode));
                QDesktopServices::openUrl(QUrl(uri));
                poll_->start((interval > 0 ? interval : 5) * 1000);
            });
    connect(api_, &Api::tokenReceived, this,
            [this](const QString &t, const QJsonObject &user) {
                poll_->stop();
                token_->setText(t);
                authStatus_->setText(QStringLiteral("登录成功: ") + user.value("email").toString());
                applyToConfig();
                st_->save();
            });
    connect(api_, &Api::failed, this, [this](const QString &e) {
        poll_->stop();
        authStatus_->setText(QStringLiteral("失败: ") + e);
    });

    connect(deviceBtn, &QPushButton::clicked, this, [this]() {
        authStatus_->setText(QStringLiteral("请求设备码…"));
        api_->deviceStart(ClientConfig::httpBaseFromWs(server_->text().trimmed()));
    });
    connect(saveBtn, &QPushButton::clicked, this, [this]() {
        applyToConfig();
        st_->save();
        authStatus_->setText(QStringLiteral("已保存"));
    });
    connect(addBtn, &QPushButton::clicked, this, [this]() {
        TunnelEditorDialog dlg(TunnelDef{QStringLiteral("web1"), QStringLiteral("http"), QString(),
                                         QStringLiteral("/web"), QStringLiteral("127.0.0.1:8080")},
                               true, this);
        if (dlg.exec() == QDialog::Accepted) {
            st_->addTunnel(dlg.result());
            refresh();
        }
    });
    connect(editBtn, &QPushButton::clicked, this, [this]() {
        const int r = tunnelTable_->currentRow();
        if (r < 0 || r >= st_->config().tunnels.size())
            return;
        const TunnelDef cur = st_->config().tunnels[r];
        TunnelEditorDialog dlg(cur, false, this);
        if (dlg.exec() == QDialog::Accepted) {
            st_->updateTunnel(cur.tunnelId, dlg.result());
            refresh();
        }
    });
    connect(delBtn, &QPushButton::clicked, this, [this]() {
        const int r = tunnelTable_->currentRow();
        if (r < 0 || r >= st_->config().tunnels.size())
            return;
        const QString id = st_->config().tunnels[r].tunnelId;
        if (QMessageBox::question(this, QStringLiteral("删除"),
                                  QStringLiteral("删除隧道 %1？").arg(id)) == QMessageBox::Yes) {
            st_->removeTunnel(id);
            refresh();
        }
    });

    connect(st_, &AppState::tunnelsChanged, this, &SettingsPage::refresh);
    refresh();
}

void SettingsPage::applyToConfig() {
    auto &c = st_->config();
    c.serverUrl = server_->text().trimmed();
    c.apiToken = token_->text().trimmed();
    c.clientId = clientId_->text().trimmed();
    c.baseDomain = base_->text().trimmed();
    c.autoReconnect = autoReconnect_->isChecked();
}

void SettingsPage::refresh() {
    const auto &c = st_->config();
    if (!server_->hasFocus())
        server_->setText(c.serverUrl);
    if (!token_->hasFocus())
        token_->setText(c.apiToken);
    if (!clientId_->hasFocus())
        clientId_->setText(c.clientId);
    if (!base_->hasFocus())
        base_->setText(c.baseDomain);
    autoReconnect_->setChecked(c.autoReconnect);

    tunnelTable_->setRowCount(c.tunnels.size());
    for (int i = 0; i < c.tunnels.size(); ++i) {
        const auto &t = c.tunnels[i];
        tunnelTable_->setItem(i, 0, new QTableWidgetItem(t.tunnelId));
        tunnelTable_->setItem(i, 1, new QTableWidgetItem(t.proto));
        tunnelTable_->setItem(i, 2, new QTableWidgetItem(t.subdomain));
        tunnelTable_->setItem(i, 3, new QTableWidgetItem(t.pathPrefix));
        tunnelTable_->setItem(i, 4, new QTableWidgetItem(t.localAddr));
    }
}

} // namespace gui
