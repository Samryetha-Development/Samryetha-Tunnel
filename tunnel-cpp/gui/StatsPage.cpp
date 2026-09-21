#include "StatsPage.hpp"
#include "AppState.hpp"
#include "Theme.hpp"
#include "Ui.hpp"

#include <QBarCategoryAxis>
#include <QBarSeries>
#include <QBarSet>
#include <QChart>
#include <QChartView>
#include <QDateTime>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QTableWidget>
#include <QTimer>
#include <QValueAxis>
#include <QVBoxLayout>

namespace gui {

static void styleChart(QChart *chart, const QColor &accent, bool verticalGrid) {
    chart->setBackgroundVisible(false);
    chart->setPlotAreaBackgroundVisible(false);
    chart->legend()->hide();
    chart->setTitleBrush(QBrush(QColor("#93a1b5")));
    chart->setMargins(QMargins(4, 4, 4, 4));
    const auto axes = chart->axes();
    for (QAbstractAxis *a : axes) {
        if (auto *va = qobject_cast<QValueAxis *>(a)) {
            va->setLabelsColor(QColor("#7d8ba1"));
            va->setGridLineColor(QColor("#1c2634"));
            va->setLineVisible(false);
            va->setGridLineVisible(verticalGrid);
        } else if (auto *ca = qobject_cast<QBarCategoryAxis *>(a)) {
            ca->setLabelsColor(QColor("#7d8ba1"));
            ca->setGridLineVisible(false);
            ca->setLineVisible(false);
        }
    }
    Q_UNUSED(accent);
}

StatsPage::StatsPage(AppState *st, QWidget *parent) : QWidget(parent), st_(st) {
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(34, 30, 34, 24);
    root->setSpacing(0);

    root->addWidget(ui::eyebrow(QStringLiteral("analytics")));
    root->addWidget(ui::pageTitle(QStringLiteral("流量")));
    root->addSpacing(22);

    auto *tiles = new QHBoxLayout;
    tiles->setSpacing(60);
    tiles->addWidget(ui::statTile(QStringLiteral("累计请求"), "#4c8dff", &vReqs_));
    tiles->addWidget(ui::statTile(QStringLiteral("成功率"), "#3ddc84", &vRate_));
    tiles->addWidget(ui::statTile(QStringLiteral("平均延迟"), "#a78bfa", &vLatency_));
    tiles->addWidget(ui::statTile(QStringLiteral("下行流量"), "#f5b74e", &vBytes_));
    root->addLayout(tiles);

    auto *minuteCard = ui::card();
    auto *mc = new QVBoxLayout(minuteCard);
    mc->setContentsMargins(14, 12, 14, 10);
    minuteChart_ = new QChartView;
    minuteChart_->setMinimumHeight(190);
    minuteChart_->setRenderHint(QPainter::Antialiasing);
    mc->addWidget(minuteChart_);
    root->addWidget(minuteCard);

    auto *mid = new QHBoxLayout;
    mid->setSpacing(40);
    root->addSpacing(26);

    auto *tunnelCard = ui::card();
    auto *tc = new QVBoxLayout(tunnelCard);
    tc->setContentsMargins(14, 12, 14, 10);
    tunnelChart_ = new QChartView;
    tunnelChart_->setRenderHint(QPainter::Antialiasing);
    tc->addWidget(tunnelChart_);
    mid->addWidget(tunnelCard, 1);

    auto *recentCard = ui::card();
    auto *rc = new QVBoxLayout(recentCard);
    rc->setContentsMargins(14, 12, 14, 10);
    rc->addWidget(ui::eyebrow(QStringLiteral("recent")));
    recent_ = new QTableWidget;
    recent_->setColumnCount(5);
    recent_->setHorizontalHeaderLabels({QStringLiteral("时间"), QStringLiteral("隧道"),
                                        QStringLiteral("状态"), QStringLiteral("字节"),
                                        QStringLiteral("延迟")});
    recent_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    recent_->verticalHeader()->setVisible(false);
    recent_->setShowGrid(false);
    recent_->setAlternatingRowColors(true);
    recent_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    rc->addWidget(recent_, 1);
    mid->addWidget(recentCard, 1);
    root->addLayout(mid, 1);

    chartTimer_ = new QTimer(this);
    chartTimer_->setInterval(2000);
    connect(chartTimer_, &QTimer::timeout, this, &StatsPage::refreshCharts);
    chartTimer_->start();

    connect(st_, &AppState::statsChanged, this, &StatsPage::refreshTable);
    refreshTable();
    refreshCharts();
}

void StatsPage::refreshCharts() {
    const auto &events = st_->events();

    QVector<int> buckets(30, 0);
    const QDateTime now = QDateTime::currentDateTime();
    const QDateTime start = now.addSecs(-29 * 60);
    for (const auto &e : events) {
        const qint64 secs = start.secsTo(e.at);
        if (secs < 0)
            continue;
        const int idx = static_cast<int>(secs / 60);
        if (idx >= 0 && idx < 30)
            buckets[idx] += 1;
    }
    auto *set = new QBarSet(QStringLiteral("请求"));
    set->setColor(QColor("#3ddc84"));
    set->setBorderColor(Qt::transparent);
    QStringList cats;
    for (int i = 0; i < 30; ++i) {
        *set << buckets[i];
        const QDateTime t = start.addSecs(i * 60);
        cats << ((i % 10 == 0) ? t.toString(QStringLiteral("HH:mm")) : QString());
    }
    auto *series = new QBarSeries;
    series->append(set);
    auto *chart = new QChart;
    chart->addSeries(series);
    chart->setTitle(QStringLiteral("最近 30 分钟 · 每分钟请求"));
    auto *ax = new QBarCategoryAxis;
    ax->append(cats);
    chart->addAxis(ax, Qt::AlignBottom);
    series->attachAxis(ax);
    auto *ay = new QValueAxis;
    ay->setLabelFormat("%d");
    ay->setRange(0, std::max(1, *std::max_element(buckets.begin(), buckets.end())) * 1.2);
    chart->addAxis(ay, Qt::AlignLeft);
    series->attachAxis(ay);
    styleChart(chart, QColor("#3ddc84"), true);
    minuteChart_->setChart(chart);

    QMap<QString, int> counts;
    for (const auto &e : events)
        counts[e.tunnelId] += 1;
    auto *tset = new QBarSet(QStringLiteral("请求"));
    tset->setColor(QColor("#4c8dff"));
    tset->setBorderColor(Qt::transparent);
    QStringList tcats;
    int maxCount = 1;
    for (auto it = counts.constBegin(); it != counts.constEnd(); ++it) {
        *tset << it.value();
        tcats << it.key();
        maxCount = std::max(maxCount, it.value());
    }
    auto *tseries = new QBarSeries;
    tseries->append(tset);
    auto *tchart = new QChart;
    tchart->addSeries(tseries);
    tchart->setTitle(QStringLiteral("按隧道分布"));
    auto *tax = new QBarCategoryAxis;
    tax->append(tcats);
    tchart->addAxis(tax, Qt::AlignBottom);
    tseries->attachAxis(tax);
    auto *tay = new QValueAxis;
    tay->setLabelFormat("%d");
    tay->setRange(0, maxCount * 1.2);
    tchart->addAxis(tay, Qt::AlignLeft);
    tseries->attachAxis(tay);
    styleChart(tchart, QColor("#4c8dff"), true);
    tunnelChart_->setChart(tchart);

    int total = events.size(), ok = 0, latency = 0, n = 0;
    qint64 bytes = 0;
    for (const auto &e : events) {
        if (e.status < 500)
            ++ok;
        bytes += e.bytes;
        if (e.ms > 0) {
            latency += e.ms;
            ++n;
        }
    }
    vReqs_->setText(QString::number(total));
    vRate_->setText(total ? QStringLiteral("%1%").arg(100.0 * ok / total, 0, 'f', 1)
                          : QStringLiteral("—"));
    vLatency_->setText(n ? QStringLiteral("%1ms").arg(latency / n) : QStringLiteral("—"));
    vBytes_->setText(QStringLiteral("%1MB").arg(bytes / 1024.0 / 1024.0, 0, 'f', 2));
}

void StatsPage::refreshTable() {
    const auto &events = st_->events();
    const int rows = qMin(30, (int)events.size());
    recent_->setRowCount(rows);
    for (int i = 0; i < rows; ++i) {
        const auto &e = events[events.size() - 1 - i];
        recent_->setItem(i, 0, new QTableWidgetItem(e.at.toString(QStringLiteral("HH:mm:ss"))));
        recent_->setItem(i, 1, new QTableWidgetItem(e.tunnelId));
        recent_->setItem(i, 2, new QTableWidgetItem(QString::number(e.status)));
        recent_->setItem(i, 3, new QTableWidgetItem(QString::number(e.bytes)));
        recent_->setItem(i, 4, new QTableWidgetItem(QString::number(e.ms)));
    }
}

} // namespace gui
