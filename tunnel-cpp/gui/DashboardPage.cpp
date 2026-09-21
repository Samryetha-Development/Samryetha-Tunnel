#include "DashboardPage.hpp"
#include "AppState.hpp"
#include "Theme.hpp"
#include "TunnelEditorDialog.hpp"
#include "Ui.hpp"

#include "tunnel/Client.hpp"

#include <QClipboard>
#include <QFrame>
#include <QGridLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QStyle>
#include <QVBoxLayout>

using namespace tunnel;

namespace gui {

static QString fmtBytes(qint64 b) {
    if (b < 1024)
        return QStringLiteral("%1B").arg(b);
    if (b < 1024 * 1024)
        return QStringLiteral("%1KB").arg(b / 1024.0, 0, 'f', 1);
    return QStringLiteral("%1MB").arg(b / 1024.0 / 1024.0, 0, 'f', 2);
}

DashboardPage::DashboardPage(AppState *st, QWidget *parent) : QWidget(parent), st_(st) {
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(20, 20, 20, 20);
    root->setSpacing(16);

    // ---------- Hero ----------
    auto *hero = new QFrame;
    hero->setObjectName("hero");
    auto *hv = new QHBoxLayout(hero);
    hv->setContentsMargins(20, 18, 20, 18);
    hv->setSpacing(14);

    dot_ = new QLabel;
    dot_->setFixedSize(12, 12);

    auto *col = new QVBoxLayout;
    col->setSpacing(4);
    title_ = new QLabel(QStringLiteral("控制通道未建立"));
    title_->setObjectName("h1");
    subtitle_ = new QLabel("—");
    subtitle_->setObjectName("heroSub");
    col->addWidget(title_);
    col->addWidget(subtitle_);
    hv->addWidget(dot_);
    hv->addLayout(col);
    hv->addStretch();

    auto *testAll = new QPushButton(QStringLiteral("测试本地"));
    testAll->setObjectName("ghost");
    auto *addBtn = new QPushButton(QStringLiteral("＋ 新隧道"));
    addBtn->setObjectName("ghost");
    connectBtn_ = new QPushButton(QStringLiteral("连接"));
    connectBtn_->setObjectName("primary");
    connectBtn_->setMinimumWidth(96);
    hv->addWidget(testAll);
    hv->addWidget(addBtn);
    hv->addWidget(connectBtn_);
    root->addWidget(hero);

    // ---------- 指标 ----------
    auto *tiles = new QHBoxLayout;
    tiles->setSpacing(12);
    tiles->addWidget(ui::statTile(QStringLiteral("隧道数"), theme::kAccent, &vTunnels_));
    tiles->addWidget(ui::statTile(QStringLiteral("累计请求"), theme::kAccent2, &vReqs_));
    tiles->addWidget(ui::statTile(QStringLiteral("成功率"), theme::kPurple, &vRate_));
    tiles->addWidget(ui::statTile(QStringLiteral("下行流量"), theme::kWarn, &vBytes_));
    root->addLayout(tiles);

    // ---------- 隧道卡片 ----------
    root->addWidget(ui::sectionTitle(QStringLiteral("我的隧道")));
    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    cardsHost_ = new QWidget;
    auto *cv = new QVBoxLayout(cardsHost_);
    cv->setContentsMargins(0, 0, 6, 0);
    cv->setSpacing(12);
    scroll->setWidget(cardsHost_);
    root->addWidget(scroll, 1);

    connect(connectBtn_, &QPushButton::clicked, this, [this]() {
        if (st_->client()->isConnected())
            st_->disconnectServer();
        else
            st_->connectServer();
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
    connect(testAll, &QPushButton::clicked, this, [this]() {
        for (const auto &t : st_->config().tunnels)
            st_->testLocal(t.tunnelId, t.localAddr);
    });

    connect(st_, &AppState::stateChanged, this, &DashboardPage::refresh);
    connect(st_, &AppState::tunnelsChanged, this, &DashboardPage::refresh);
    connect(st_, &AppState::statsChanged, this, &DashboardPage::refresh);
    refresh();
}

QWidget *DashboardPage::buildTunnelCard(int index, bool connected) {
    const auto &t = st_->config().tunnels[index];
    QMap<QString, QString> urls;
    for (const auto &e : st_->effective())
        urls[e.tunnelId] = e.publicUrl;
    const auto stat = st_->stats().value(t.tunnelId);

    auto *card = new QFrame;
    card->setObjectName("tunnelCard");
    auto *v = new QVBoxLayout(card);
    v->setContentsMargins(16, 14, 16, 14);
    v->setSpacing(9);

    // 第一行：ID + 类型 + 状态
    auto *top = new QHBoxLayout;
    auto *id = new QLabel(t.tunnelId);
    id->setObjectName("h2");
    top->addWidget(id);
    top->addWidget(ui::chip(t.proto));
    auto *pill = ui::pill(connected ? QStringLiteral("● 在线") : QStringLiteral("○ 离线"), connected);
    top->addWidget(pill);
    top->addStretch();
    if (stat.count > 0) {
        auto *s = ui::chip(QStringLiteral("%1 次 · %2").arg(stat.count).arg(fmtBytes(stat.bytes)));
        top->addWidget(s);
    }
    v->addLayout(top);

    // 路由
    const QString url = urls.value(t.tunnelId, connected ? QStringLiteral("—") : QStringLiteral("未连接"));
    auto *route = new QHBoxLayout;
    auto *routeIcon = new QLabel(QStringLiteral("🌐"));
    auto *routeText = new QLabel(url);
    routeText->setObjectName("mono");
    routeText->setTextInteractionFlags(Qt::TextSelectableByMouse);
    auto *copy = new QPushButton(QStringLiteral("复制"));
    copy->setObjectName("ghost");
    copy->setFixedHeight(26);
    route->addWidget(routeIcon);
    route->addWidget(routeText, 1);
    route->addWidget(copy);
    v->addLayout(route);

    // 本地
    auto *local = new QHBoxLayout;
    auto *localIcon = new QLabel(QStringLiteral("🏠"));
    auto *localText = new QLabel(t.localAddr);
    localText->setObjectName("mono");
    local->addWidget(localIcon);
    local->addWidget(localText);
    local->addStretch();
    v->addLayout(local);

    // 操作
    auto *acts = new QHBoxLayout;
    auto *test = new QPushButton(QStringLiteral("本地测试"));
    test->setObjectName("ghost");
    auto *edit = new QPushButton(QStringLiteral("编辑"));
    edit->setObjectName("ghost");
    auto *del = new QPushButton(QStringLiteral("删除"));
    del->setObjectName("danger");
    acts->addWidget(test);
    acts->addWidget(edit);
    acts->addStretch();
    acts->addWidget(del);
    v->addLayout(acts);

    connect(copy, &QPushButton::clicked, this, [url]() {
        QGuiApplication::clipboard()->setText(url);
    });
    connect(test, &QPushButton::clicked, this, [this, t]() { st_->testLocal(t.tunnelId, t.localAddr); });
    connect(edit, &QPushButton::clicked, this, [this, t]() {
        TunnelEditorDialog dlg(t, false, this);
        if (dlg.exec() == QDialog::Accepted) {
            st_->updateTunnel(t.tunnelId, dlg.result());
            refresh();
        }
    });
    connect(del, &QPushButton::clicked, this, [this, id = t.tunnelId]() {
        if (QMessageBox::question(this, QStringLiteral("删除"),
                                  QStringLiteral("删除隧道 %1？").arg(id)) == QMessageBox::Yes) {
            st_->removeTunnel(id);
            refresh();
        }
    });
    return card;
}

void DashboardPage::rebuildCards(bool connected) {
    auto *cv = qobject_cast<QVBoxLayout *>(cardsHost_->layout());
    while (QLayoutItem *item = cv->takeAt(0)) {
        if (item->widget())
            item->widget()->deleteLater();
        delete item;
    }
    if (st_->config().tunnels.isEmpty()) {
        auto *empty = new QLabel(QStringLiteral("还没有隧道 —— 点右上「＋ 新隧道」，把内网服务暴露出去。"));
        empty->setObjectName("muted");
        empty->setAlignment(Qt::AlignCenter);
        empty->setMinimumHeight(120);
        cv->addWidget(empty);
        cv->addStretch();
        return;
    }
    for (int i = 0; i < st_->config().tunnels.size(); ++i)
        cv->addWidget(buildTunnelCard(i, connected));
    cv->addStretch();
}

void DashboardPage::refresh() {
    const bool connected = st_->client()->isConnected();
    dot_->setStyleSheet(QStringLiteral("background:%1;border-radius:6px;")
                            .arg(connected ? theme::kAccent : "#55617a"));
    title_->setText(connected ? QStringLiteral("控制通道正常") : QStringLiteral("控制通道未建立"));
    subtitle_->setText(st_->config().serverUrl + QStringLiteral("  ·  ") + st_->config().clientId);
    connectBtn_->setText(connected ? QStringLiteral("断开") : QStringLiteral("连接"));
    connectBtn_->setObjectName(connected ? "danger" : "primary");
    connectBtn_->style()->unpolish(connectBtn_);
    connectBtn_->style()->polish(connectBtn_);

    int total = 0, ok = 0;
    qint64 bytes = 0;
    for (const auto &e : st_->events()) {
        ++total;
        bytes += e.bytes;
        if (e.status < 500)
            ++ok;
    }
    vTunnels_->setText(QString::number(st_->config().tunnels.size()));
    vReqs_->setText(QString::number(total));
    vRate_->setText(total ? QStringLiteral("%1%").arg(100.0 * ok / total, 0, 'f', 1)
                          : QStringLiteral("—"));
    vBytes_->setText(fmtBytes(bytes));

    rebuildCards(connected);
}

} // namespace gui
