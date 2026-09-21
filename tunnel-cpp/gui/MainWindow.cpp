#include "MainWindow.hpp"
#include "AppState.hpp"
#include "DashboardPage.hpp"
#include "LogsPage.hpp"
#include "SettingsPage.hpp"
#include "StatsPage.hpp"
#include "Ui.hpp"

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
    resize(1080, 720);

    auto *central = new QWidget;
    auto *root = new QHBoxLayout(central);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // ---------- 侧栏 ----------
    auto *sidebar = new QWidget;
    sidebar->setObjectName("sidebar");
    sidebar->setFixedWidth(238);
    auto *sv = new QVBoxLayout(sidebar);
    sv->setContentsMargins(14, 16, 14, 14);
    sv->setSpacing(12);

    // 品牌
    auto *brandRow = new QHBoxLayout;
    brandRow->setSpacing(9);
    auto *mark = new QLabel(QStringLiteral("T"));
    mark->setObjectName("brandMark");
    mark->setFixedSize(32, 32);
    mark->setAlignment(Qt::AlignCenter);
    auto *brandCol = new QVBoxLayout;
    brandCol->setSpacing(0);
    auto *name = new QLabel(QStringLiteral("Tunnel"));
    name->setObjectName("brandName");
    auto *domain = new QLabel(st_->config().baseDomain);
    domain->setObjectName("muted");
    domain->setStyleSheet(QStringLiteral("font-size:10px;"));
    brandCol->addWidget(name);
    brandCol->addWidget(domain);
    brandRow->addWidget(mark);
    brandRow->addLayout(brandCol);
    brandRow->addStretch();
    sv->addLayout(brandRow);

    // 状态卡
    auto *statusCard = new QFrame;
    statusCard->setObjectName("tile");
    auto *sc = new QGridLayout(statusCard);
    sc->setContentsMargins(12, 10, 12, 10);
    sc->setHorizontalSpacing(8);
    sc->setVerticalSpacing(2);
    statusDot_ = new QLabel;
    statusDot_->setFixedSize(9, 9);
    statusTitle_ = new QLabel(QStringLiteral("未连接"));
    statusTitle_->setStyleSheet(QStringLiteral("font-weight:650;"));
    statusSub_ = new QLabel("—");
    statusSub_->setObjectName("muted");
    statusSub_->setStyleSheet(QStringLiteral("font-size:10px;"));
    sc->addWidget(statusDot_, 0, 0);
    sc->addWidget(statusTitle_, 0, 1);
    sc->addWidget(statusSub_, 1, 0, 1, 2);
    sv->addWidget(statusCard);

    sv->addWidget(ui::sectionTitle(QStringLiteral("工作区")));

    auto *nav = new QListWidget;
    nav->setObjectName("nav");
    nav->addItems({QStringLiteral("▦   隧道"), QStringLiteral("☰   日志"),
                   QStringLiteral("◔   流量"), QStringLiteral("⚙   设置")});
    nav->setCurrentRow(0);
    nav->setFixedHeight(4 * 40 + 10);
    sv->addWidget(nav);

    sv->addWidget(ui::sectionTitle(QStringLiteral("隧道")));
    tunnelList_ = new QListWidget;
    tunnelList_->setObjectName("tunnelList");
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
    const bool connecting = st_->client()->config().serverUrl.size() > 0 && !connected;
    statusDot_->setStyleSheet(QStringLiteral("background:%1;border-radius:4px;")
                                  .arg(connected ? "#3ddc84" : (connecting ? "#f5b74e" : "#55617a")));
    statusTitle_->setText(connected ? QStringLiteral("已连接") : QStringLiteral("未连接"));
    statusSub_->setText(connected ? QStringLiteral("运行中 · %1 秒").arg(st_->uptimeSecs())
                                  : st_->config().clientId);

    tunnelList_->clear();
    const auto &stats = st_->stats();
    for (const auto &t : st_->config().tunnels) {
        const auto s = stats.value(t.tunnelId);
        tunnelList_->addItem(QStringLiteral("•  %1        %2").arg(t.tunnelId).arg(s.count));
    }
}

} // namespace gui
