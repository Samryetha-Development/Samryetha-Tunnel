#include "StatsPage.hpp"
#include "AppState.hpp"

#include <QBarCategoryAxis>
#include <QBarSeries>
#include <QBarSet>
#include <QChart>
#include <QChartView>
#include <QDateTime>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QTableWidget>
#include <QTimer>
#include <QValueAxis>
#include <QVBoxLayout>

namespace gui {

static QFrame *tile(const QString &label, QLabel **out) {
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
    *out = val;
    return f;
}

StatsPage::StatsPage(AppState *st, QWidget *parent) : QWidget(parent), st_(st) {
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(18, 18, 18, 18);
    root->setSpacing(12);

    auto *tiles = new QHBoxLayout;
    tiles->setSpacing(10);
    tiles->addWidget(tile(QStringLiteral("累计请求"), &vReqs_));
    tiles->addWidget(tile(QStringLiteral("成功率"), &vRate_));
    tiles->addWidget(tile(QStringLiteral("平均延迟"), &vLatency_));
    tiles->addWidget(tile(QStringLiteral("下行流量"), &vBytes_));
    root->addLayout(tiles);

    minuteChart_ = new QChartView;
    minuteChart_->setMinimumHeight(180);
    root->addWidget(minuteChart_);

    auto *mid = new QHBoxLayout;
    tunnelChart_ = new QChartView;
    tunnelChart_->setMinimumHeight(180);
    mid->addWidget(tunnelChart_, 1);

    recent_ = new QTableWidget;
    recent_->setColumnCount(5);
    recent_->setHorizontalHeaderLabels({QStringLiteral("时间"), QStringLiteral("隧道"),
                                        QStringLiteral("状态"), QStringLiteral("字节"),
                                        QStringLiteral("延迟")});
    recent_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    recent_->verticalHeader()->setVisible(false);
    recent_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    mid->addWidget(recent_, 1);
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

    // 最近 30 分钟，每分钟请求数
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
    chart->setBackgroundVisible(false);
    chart->legend()->hide();
    auto *ax = new QBarCategoryAxis;
    ax->append(cats);
    chart->addAxis(ax, Qt::AlignBottom);
    series->attachAxis(ax);
    auto *ay = new QValueAxis;
    ay->setLabelFormat("%d");
    chart->addAxis(ay, Qt::AlignLeft);
    series->attachAxis(ay);
    minuteChart_->setChart(chart);

    // 按隧道分布
    QMap<QString, int> counts;
    for (const auto &e : events)
        counts[e.tunnelId] += 1;
    auto *tset = new QBarSet(QStringLiteral("请求"));
    QStringList tcats;
    for (auto it = counts.constBegin(); it != counts.constEnd(); ++it) {
        *tset << it.value();
        tcats << it.key();
    }
    auto *tseries = new QBarSeries;
    tseries->append(tset);
    auto *tchart = new QChart;
    tchart->addSeries(tseries);
    tchart->setTitle(QStringLiteral("按隧道分布"));
    tchart->setBackgroundVisible(false);
    tchart->legend()->hide();
    auto *tax = new QBarCategoryAxis;
    tax->append(tcats);
    tchart->addAxis(tax, Qt::AlignBottom);
    tseries->attachAxis(tax);
    auto *tay = new QValueAxis;
    tay->setLabelFormat("%d");
    tchart->addAxis(tay, Qt::AlignLeft);
    tseries->attachAxis(tay);
    tunnelChart_->setChart(tchart);

    // 汇总
    int total = events.size();
    int ok = 0;
    qint64 bytes = 0;
    int latency = 0;
    int n = 0;
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
    const int rows = qMin(30, events.size());
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
