#include "DashboardPage.hpp"
#include "AppState.hpp"
#include "TunnelEditorDialog.hpp"

#include "tunnel/Client.hpp"

#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QStyle>
#include <QTableWidget>
#include <QVBoxLayout>

using namespace tunnel;

namespace gui {

static QFrame *makeTile(const QString &label, QLabel **valueOut) {
    auto *f = new QFrame;
    f->setObjectName("tile");
    auto *v = new QVBoxLayout(f);
    v->setContentsMargins(14, 12, 14, 12);
    v->setSpacing(4);
    auto *l = new QLabel(label);
    l->setObjectName("cardLabel");
    auto *val = new QLabel("—");
    val->setObjectName("cardValue");
    v->addWidget(l);
    v->addWidget(val);
    *valueOut = val;
    return f;
}

DashboardPage::DashboardPage(AppState *st, QWidget *parent) : QWidget(parent), st_(st) {
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(18, 18, 18, 18);
    root->setSpacing(14);

    // 头部
    auto *header = new QHBoxLayout;
    dot_ = new QLabel;
    dot_->setFixedSize(10, 10);
    auto *titleCol = new QVBoxLayout;
    titleCol->setSpacing(2);
    title_ = new QLabel(QStringLiteral("控制通道未建立"));
    title_->setStyleSheet(QStringLiteral("font-size:16px;font-weight:600;"));
    subtitle_ = new QLabel("—");
    subtitle_->setObjectName("muted");
    subtitle_->setObjectName("mono");
    titleCol->addWidget(title_);
    titleCol->addWidget(subtitle_);
    header->addWidget(dot_);
    header->addLayout(titleCol);
    header->addStretch();

    auto *testAll = new QPushButton(QStringLiteral("测试本地"));
    auto *addBtn = new QPushButton(QStringLiteral("＋ 新隧道"));
    connectBtn_ = new QPushButton(QStringLiteral("连接"));
    connectBtn_->setObjectName("primary");
    header->addWidget(testAll);
    header->addWidget(addBtn);
    header->addWidget(connectBtn_);
    root->addLayout(header);

    // 指标
    auto *tiles = new QHBoxLayout;
    tiles->setSpacing(10);
    tiles->addWidget(makeTile(QStringLiteral("隧道数"), &vTunnels_));
    tiles->addWidget(makeTile(QStringLiteral("累计请求"), &vReqs_));
    tiles->addWidget(makeTile(QStringLiteral("成功率"), &vRate_));
    tiles->addWidget(makeTile(QStringLiteral("下行流量"), &vBytes_));
    root->addLayout(tiles);

    // 隧道表
    table_ = new QTableWidget;
    table_->setColumnCount(5);
    table_->setHorizontalHeaderLabels({QStringLiteral("ID"), QStringLiteral("公网地址"),
                                       QStringLiteral("本地"), QStringLiteral("状态"),
                                       QStringLiteral("操作")});
    table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    table_->verticalHeader()->setVisible(false);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    root->addWidget(table_, 1);

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

void DashboardPage::refresh() {
    const bool connected = st_->client()->isConnected();
    dot_->setStyleSheet(QStringLiteral("background:%1;border-radius:5px;")
                            .arg(connected ? "#3ba55d" : "#5b6675"));
    title_->setText(connected ? QStringLiteral("控制通道正常") : QStringLiteral("控制通道未建立"));
    subtitle_->setText(st_->config().serverUrl + QStringLiteral(" · ") + st_->config().clientId);
    connectBtn_->setText(connected ? QStringLiteral("断开") : QStringLiteral("连接"));
    connectBtn_->setObjectName(connected ? "danger" : "primary");
    connectBtn_->style()->unpolish(connectBtn_);
    connectBtn_->style()->polish(connectBtn_);

    int total = 0;
    qint64 bytes = 0;
    int ok = 0;
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
    double mb = bytes / 1024.0 / 1024.0;
    vBytes_->setText(bytes < 1024 * 1024 ? QStringLiteral("%1KB").arg(bytes / 1024.0, 0, 'f', 1)
                                         : QStringLiteral("%1MB").arg(mb, 0, 'f', 1));

    // 公网地址映射
    QMap<QString, QString> urls;
    for (const auto &e : st_->effective())
        urls[e.tunnelId] = e.publicUrl;

    const auto &tunnels = st_->config().tunnels;
    table_->setRowCount(tunnels.size());
    for (int r = 0; r < tunnels.size(); ++r) {
        const auto &t = tunnels[r];
        table_->setItem(r, 0, new QTableWidgetItem(t.tunnelId));
        const QString url = urls.value(t.tunnelId,
                                       connected ? QStringLiteral("—") : QStringLiteral("未连接"));
        table_->setItem(r, 1, new QTableWidgetItem(url));
        table_->setItem(r, 2, new QTableWidgetItem(t.localAddr));
        const auto st = st_->stats().value(t.tunnelId);
        table_->setItem(r, 3, new QTableWidgetItem(
                                   connected ? QStringLiteral("在线 · %1 次 · %2ms")
                                                   .arg(st.count)
                                                   .arg(st.lastMs)
                                             : QStringLiteral("离线")));
        auto *box = new QWidget;
        auto *hb = new QHBoxLayout(box);
        hb->setContentsMargins(0, 0, 0, 0);
        hb->setSpacing(4);
        auto *test = new QPushButton(QStringLiteral("测试"));
        auto *edit = new QPushButton(QStringLiteral("编辑"));
        auto *del = new QPushButton(QStringLiteral("删除"));
        del->setObjectName("danger");
        hb->addWidget(test);
        hb->addWidget(edit);
        hb->addWidget(del);
        const QString id = t.tunnelId;
        const QString local = t.localAddr;
        connect(test, &QPushButton::clicked, this, [this, id, local]() { st_->testLocal(id, local); });
        connect(edit, &QPushButton::clicked, this, [this, t]() {
            TunnelEditorDialog dlg(t, false, this);
            if (dlg.exec() == QDialog::Accepted) {
                st_->updateTunnel(t.tunnelId, dlg.result());
                refresh();
            }
        });
        connect(del, &QPushButton::clicked, this, [this, id]() {
            if (QMessageBox::question(this, QStringLiteral("删除"),
                                      QStringLiteral("删除隧道 %1？").arg(id)) == QMessageBox::Yes) {
                st_->removeTunnel(id);
                refresh();
            }
        });
        table_->setCellWidget(r, 4, box);
    }
    table_->resizeColumnsToContents();
    table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
}

} // namespace gui
