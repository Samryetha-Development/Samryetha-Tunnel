#include "MainWindow.hpp"
#include "AppState.hpp"
#include "DashboardPage.hpp"
#include "LogsPage.hpp"
#include "AdminPage.hpp"
#include "SettingsPage.hpp"
#include "TokensPage.hpp"
#include "UsagePage.hpp"
#include "StatsPage.hpp"
#include "Ui.hpp"

#include "tunnel/Client.hpp"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QStackedWidget>
#include <QTimer>
#include <QVBoxLayout>

namespace gui {

MainWindow::MainWindow(AppState *st, QWidget *parent) : QMainWindow(parent), st_(st) {
    setWindowTitle(QStringLiteral("Samryetha Tunnel"));
    resize(1080, 720);

    auto *central = new QWidget;
    auto *root = new QHBoxLayout(central);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // ---------- 侧栏 ----------
    auto *sidebar = new QWidget;
    sidebar->setObjectName("sidebar");
    sidebar->setFixedWidth(232);
    auto *sv = new QVBoxLayout(sidebar);
    sv->setContentsMargins(18, 20, 16, 16);
    sv->setSpacing(0);

    auto *wordmark = new QLabel(QStringLiteral("Tunnel"));
    wordmark->setObjectName("wordmark");
    auto *domain = new QLabel(st_->config().baseDomain);
    domain->setObjectName("faint");
    sv->addWidget(wordmark);
    sv->addWidget(domain);
    sv->addSpacing(20);

    // 状态（纯文本，无卡片）
    auto *statusRow = new QHBoxLayout;
    statusRow->setSpacing(8);
    statusDot_ = new QLabel;
    statusDot_->setFixedSize(8, 8);
    statusTitle_ = new QLabel(QStringLiteral("未连接"));
    statusTitle_->setObjectName("muted");
    statusRow->addWidget(statusDot_);
    statusRow->addWidget(statusTitle_);
    statusRow->addStretch();
    sv->addLayout(statusRow);
    statusSub_ = new QLabel("—");
    statusSub_->setObjectName("faint");
    sv->addWidget(statusSub_);
    sv->addSpacing(22);

    sv->addWidget(ui::eyebrow(QStringLiteral("workspace")));
    sv->addSpacing(8);
    auto *nav = new QListWidget;
    nav->setObjectName("nav");
    nav->addItems({QStringLiteral("隧道"), QStringLiteral("Token"),
                   QStringLiteral("用量"), QStringLiteral("日志"),
                   QStringLiteral("流量"), QStringLiteral("管理"),
                   QStringLiteral("设置")});
    nav->setCurrentRow(0);
    nav->setFixedHeight(7 * 36 + 6);
    adminItem_ = nav->item(5);
    sv->addWidget(nav);

    sv->addSpacing(22);
    sv->addWidget(ui::eyebrow(QStringLiteral("tunnels")));
    sv->addSpacing(6);
    tunnelList_ = new QListWidget;
    tunnelList_->setObjectName("rail");
    tunnelList_->setSelectionMode(QAbstractItemView::NoSelection);
    sv->addWidget(tunnelList_, 1);

    root->addWidget(sidebar);

    // ---------- 页面 ----------
    stack_ = new QStackedWidget;
    stack_->addWidget(new DashboardPage(st_, this));
    stack_->addWidget(new TokensPage(st_, this));
    stack_->addWidget(new UsagePage(st_, this));
    stack_->addWidget(new LogsPage(st_, this));
    stack_->addWidget(new StatsPage(st_, this));
    stack_->addWidget(new AdminPage(st_, this));
    stack_->addWidget(new SettingsPage(st_, this));
    root->addWidget(stack_, 1);

    setCentralWidget(central);

    connect(nav, &QListWidget::currentRowChanged, stack_, &QStackedWidget::setCurrentIndex);
    connect(st_, &AppState::stateChanged, this, &MainWindow::refreshStatus);
    connect(st_, &AppState::tunnelsChanged, this, &MainWindow::refreshStatus);
    connect(st_, &AppState::statsChanged, this, &MainWindow::refreshStatus);
    connect(st_, &AppState::meChanged, this, &MainWindow::updateAdminVisibility);

    tick_ = new QTimer(this);
    tick_->setInterval(1000);
    connect(tick_, &QTimer::timeout, this, &MainWindow::refreshStatus);
    tick_->start();

    refreshStatus();
    updateAdminVisibility();
}

void MainWindow::updateAdminVisibility() {
    if (!adminItem_)
        return;
    const bool admin = st_->isAdmin();
    adminItem_->setHidden(!admin);
    if (!admin)
        stack_->setCurrentIndex(0);
}

void MainWindow::refreshStatus() {
    const bool connected = st_->client()->isConnected();
    statusDot_->setStyleSheet(QStringLiteral("background:%1;border-radius:4px;")
                                  .arg(connected ? "#3ddc84" : "#4a4a4a"));
    statusTitle_->setText(connected ? QStringLiteral("已连接") : QStringLiteral("未连接"));
    statusSub_->setText(connected ? QStringLiteral("运行中 · %1 秒").arg(st_->uptimeSecs())
                                  : st_->config().clientId);

    tunnelList_->clear();
    const auto &stats = st_->stats();
    for (const auto &t : st_->config().tunnels) {
        const auto s = stats.value(t.tunnelId);
        auto *row = new QWidget;
        auto *h = new QHBoxLayout(row);
        h->setContentsMargins(2, 1, 2, 1);
        auto *name = new QLabel(t.tunnelId);
        name->setObjectName("mono");
        auto *count = new QLabel(QString::number(s.count));
        count->setObjectName("faint");
        h->addWidget(name);
        h->addStretch();
        h->addWidget(count);
        auto *item = new QListWidgetItem;
        item->setSizeHint(QSize(0, 30));
        tunnelList_->addItem(item);
        tunnelList_->setItemWidget(item, row);
    }
}

} // namespace gui
