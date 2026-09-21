#include "MainWindow.hpp"
#include "AppState.hpp"
#include "DashboardPage.hpp"
#include "LogsPage.hpp"
#include "SettingsPage.hpp"
#include "StatsPage.hpp"

#include "tunnel/Client.hpp"

#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QStackedWidget>
#include <QTimer>
#include <QVBoxLayout>

namespace gui {

MainWindow::MainWindow(AppState *st, QWidget *parent) : QMainWindow(parent), st_(st) {
    setWindowTitle(QStringLiteral("Samryetha Tunnel"));
    resize(1040, 680);

    auto *central = new QWidget;
    auto *root = new QHBoxLayout(central);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // ---------- 侧栏 ----------
    auto *sidebar = new QWidget;
    sidebar->setFixedWidth(226);
    sidebar->setStyleSheet(QStringLiteral("background:#0e131c;border-right:1px solid #1b2330;"));
    auto *sv = new QVBoxLayout(sidebar);
    sv->setContentsMargins(12, 14, 12, 12);
    sv->setSpacing(10);

    auto *brand = new QLabel(QStringLiteral("Tunnel"));
    brand->setStyleSheet(QStringLiteral("font-size:15px;font-weight:700;"));
    auto *domain = new QLabel(st_->config().baseDomain);
    domain->setObjectName("muted");
    sv->addWidget(brand);
    sv->addWidget(domain);

    // 状态卡
    auto *statusCard = new QFrame;
    statusCard->setObjectName("tile");
    auto *sc = new QGridLayout(statusCard);
    sc->setContentsMargins(10, 8, 10, 8);
    sc->setHorizontalSpacing(7);
    statusDot_ = new QLabel;
    statusDot_->setFixedSize(9, 9);
    statusTitle_ = new QLabel(QStringLiteral("未连接"));
    statusTitle_->setStyleSheet(QStringLiteral("font-weight:600;"));
    statusSub_ = new QLabel("—");
    statusSub_->setObjectName("muted");
    sc->addWidget(statusDot_, 0, 0);
    sc->addWidget(statusTitle_, 0, 1);
    sc->addWidget(statusSub_, 1, 0, 1, 2);
    sv->addWidget(statusCard);

    auto *navLabel = new QLabel(QStringLiteral("工作区"));
    navLabel->setObjectName("muted");
    sv->addWidget(navLabel);

    auto *nav = new QListWidget;
    nav->setObjectName("sidebar");
    nav->addItems({QStringLiteral("隧道"), QStringLiteral("日志"), QStringLiteral("流量"),
                   QStringLiteral("设置")});
    nav->setCurrentRow(0);
    sv->addWidget(nav);

    auto *tLabel = new QLabel(QStringLiteral("隧道"));
    tLabel->setObjectName("muted");
    sv->addWidget(tLabel);
    tunnelList_ = new QListWidget;
    tunnelList_->setObjectName("sidebar");
    sv->addWidget(tunnelList_, 1);

    root->addWidget(sidebar);

    // ---------- 页面 ----------
    stack_ = new QStackedWidget;
    stack_->addWidget(new DashboardPage(st_, this));
    stack_->addWidget(new LogsPage(st_, this));
    stack_->addWidget(new StatsPage(st_, this));
    stack_->addWidget(new SettingsPage(st_, this));
    root->addWidget(stack_, 1);

    setCentralWidget(central);

    connect(nav, &QListWidget::currentRowChanged, stack_, &QStackedWidget::setCurrentIndex);
    connect(st_, &AppState::stateChanged, this, &MainWindow::refreshStatus);
    connect(st_, &AppState::tunnelsChanged, this, &MainWindow::refreshStatus);
    connect(st_, &AppState::statsChanged, this, &MainWindow::refreshStatus);

    tick_ = new QTimer(this);
    tick_->setInterval(1000);
    connect(tick_, &QTimer::timeout, this, &MainWindow::refreshStatus);
    tick_->start();

    refreshStatus();
}

void MainWindow::refreshStatus() {
    const bool connected = st_->client()->isConnected();
    statusDot_->setStyleSheet(QStringLiteral("background:%1;border-radius:4px;")
                                  .arg(connected ? "#3ba55d" : "#5b6675"));
    statusTitle_->setText(connected ? QStringLiteral("已连接") : QStringLiteral("未连接"));
    statusSub_->setText(connected ? QStringLiteral("运行中 · %1 秒").arg(st_->uptimeSecs())
                                  : st_->config().clientId);

    tunnelList_->clear();
    const auto &stats = st_->stats();
    for (const auto &t : st_->config().tunnels) {
        const auto s = stats.value(t.tunnelId);
        tunnelList_->addItem(QStringLiteral("%1  ·  %2").arg(t.tunnelId).arg(s.count));
    }
}

} // namespace gui
