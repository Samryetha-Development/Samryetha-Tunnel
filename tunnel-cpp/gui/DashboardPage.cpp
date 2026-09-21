#include "DashboardPage.hpp"
#include "AppState.hpp"
#include "Theme.hpp"
#include "TunnelEditorDialog.hpp"
#include "Ui.hpp"

#include "tunnel/Client.hpp"

#include <QClipboard>
#include <QFrame>
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
    root->setContentsMargins(34, 30, 34, 24);
    root->setSpacing(0);

    // ---------- 页头 ----------
    auto *head = new QHBoxLayout;
    head->setSpacing(16);
    auto *col = new QVBoxLayout;
    col->setSpacing(6);
    col->addWidget(ui::eyebrow(QStringLiteral("control channel")));
    title_ = ui::pageTitle(QStringLiteral("未连接"));
    col->addWidget(title_);
    subtitle_ = new QLabel("—");
    subtitle_->setObjectName("mono");
    subtitle_->setStyleSheet(QStringLiteral("color:#6f6f6f;"));
    col->addWidget(subtitle_);
    head->addLayout(col);
    head->addStretch();

    auto *testAll = new QPushButton(QStringLiteral("测试本地"));
    testAll->setObjectName("link");
    auto *addBtn = new QPushButton(QStringLiteral("＋ 新隧道"));
    addBtn->setObjectName("link");
    connectBtn_ = new QPushButton(QStringLiteral("连接"));
    connectBtn_->setObjectName("primary");
    connectBtn_->setMinimumWidth(104);
    head->addWidget(testAll, 0, Qt::AlignBottom);
    head->addWidget(addBtn, 0, Qt::AlignBottom);
    head->addWidget(connectBtn_, 0, Qt::AlignBottom);
    root->addLayout(head);

    root->addSpacing(22);
    root->addWidget(ui::separator());
    root->addSpacing(22);

    // ---------- 总览 ----------
    root->addWidget(ui::eyebrow(QStringLiteral("overview")));
    root->addSpacing(14);
    auto *metrics = new QHBoxLayout;
    metrics->setSpacing(60);
    metrics->addWidget(ui::metric(QStringLiteral("隧道"), &vTunnels_));
    metrics->addWidget(ui::metric(QStringLiteral("累计请求"), &vReqs_));
    metrics->addWidget(ui::metric(QStringLiteral("成功率"), &vRate_));
    metrics->addWidget(ui::metric(QStringLiteral("下行流量"), &vBytes_));
    metrics->addStretch();
    root->addLayout(metrics);

    root->addSpacing(26);
    root->addWidget(ui::separator());
    root->addSpacing(22);

    // ---------- 隧道列表 ----------
    auto *tl = new QHBoxLayout;
    tl->addWidget(ui::eyebrow(QStringLiteral("tunnels")));
    tl->addStretch();
    root->addLayout(tl);
    root->addSpacing(6);

    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    cardsHost_ = new QWidget;
    auto *cv = new QVBoxLayout(cardsHost_);
    cv->setContentsMargins(0, 0, 0, 0);
    cv->setSpacing(0);
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

    auto *row = new QFrame;
    row->setObjectName("row");
    row->setAttribute(Qt::WA_Hover, true);
    auto *h = new QHBoxLayout(row);
    h->setContentsMargins(0, 16, 0, 16);
    h->setSpacing(18);

    // 左：主信息
    auto *left = new QVBoxLayout;
    left->setSpacing(7);
    auto *titleRow = new QHBoxLayout;
    titleRow->setSpacing(10);
    auto *id = new QLabel(t.tunnelId);
    id->setObjectName("sectionTitle");
    titleRow->addWidget(id);
    titleRow->addWidget(ui::chip(t.proto));
    titleRow->addWidget(ui::pill(connected ? QStringLiteral("在线") : QStringLiteral("离线"), connected));
    titleRow->addStretch();
    left->addLayout(titleRow);

    const QString url = urls.value(t.tunnelId, QStringLiteral("—"));
    auto *routeRow = new QHBoxLayout;
    routeRow->setSpacing(8);
    auto *routeText = new QLabel(url);
    routeText->setObjectName("mono");
    routeText->setTextInteractionFlags(Qt::TextSelectableByMouse);
    auto *copy = new QPushButton(QStringLiteral("复制"));
    copy->setObjectName("link");
    routeRow->addWidget(routeText);
    routeRow->addWidget(copy);
    routeRow->addStretch();
    left->addLayout(routeRow);

    auto *localText = new QLabel(t.localAddr);
    localText->setObjectName("faint");
    left->addWidget(localText);
    h->addLayout(left, 1);

    // 右：统计 + 操作
    auto *right = new QVBoxLayout;
    right->setSpacing(10);
    auto *statText = new QLabel(stat.count > 0
                                    ? QStringLiteral("%1 次 · %2 · %3ms")
                                          .arg(stat.count)
                                          .arg(fmtBytes(stat.bytes))
                                          .arg(stat.lastMs)
                                    : QStringLiteral("—"));
    statText->setObjectName("mono");
    statText->setAlignment(Qt::AlignRight);
    right->addWidget(statText);
    auto *acts = new QHBoxLayout;
    acts->setSpacing(4);
    acts->addStretch();
    auto *test = new QPushButton(QStringLiteral("本地测试"));
    test->setObjectName("link");
    auto *edit = new QPushButton(QStringLiteral("编辑"));
    edit->setObjectName("link");
    auto *del = new QPushButton(QStringLiteral("删除"));
    del->setObjectName("linkDanger");
    acts->addWidget(test);
    acts->addWidget(edit);
    acts->addWidget(del);
    right->addLayout(acts);
    h->addLayout(right);

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
    connect(del, &QPushButton::clicked, this, [this, tid = t.tunnelId]() {
        if (QMessageBox::question(this, QStringLiteral("删除"),
                                  QStringLiteral("删除隧道 %1？").arg(tid)) == QMessageBox::Yes) {
            st_->removeTunnel(tid);
            refresh();
        }
    });
    return row;
}

void DashboardPage::rebuildCards(bool connected) {
    auto *cv = qobject_cast<QVBoxLayout *>(cardsHost_->layout());
    while (QLayoutItem *item = cv->takeAt(0)) {
        if (item->widget())
            item->widget()->deleteLater();
        delete item;
    }
    if (st_->config().tunnels.isEmpty()) {
        auto *empty = new QLabel(QStringLiteral("还没有隧道。点右上「＋ 新隧道」把内网服务暴露出去。"));
        empty->setObjectName("muted");
        empty->setContentsMargins(0, 30, 0, 0);
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
    title_->setText(connected ? QStringLiteral("控制通道正常") : QStringLiteral("未连接"));
    subtitle_->setText(st_->config().serverUrl + QStringLiteral("   ·   ") + st_->config().clientId);
    connectBtn_->setText(connected ? QStringLiteral("断开") : QStringLiteral("连接"));
    connectBtn_->setStyleSheet(connected
                                   ? QStringLiteral("QPushButton{background:transparent;border:1px solid #2a2a2a;"
                                                    "color:#dcdcdc;border-radius:909px;padding:7px 16px;}"
                                                    "QPushButton:hover{border-color:#3a3a3a;color:#fff;}")
                                   : QString());

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
