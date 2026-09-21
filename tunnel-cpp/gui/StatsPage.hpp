#pragma once

#include <QWidget>

class QLabel;
class QTableWidget;
class QChartView;
class QTimer;

namespace gui {

class AppState;

class StatsPage : public QWidget {
    Q_OBJECT
public:
    explicit StatsPage(AppState *st, QWidget *parent = nullptr);

public slots:
    void refreshCharts();
    void refreshTable();

private:
    AppState *st_;
    QLabel *vReqs_ = nullptr;
    QLabel *vRate_ = nullptr;
    QLabel *vLatency_ = nullptr;
    QLabel *vBytes_ = nullptr;
    QChartView *minuteChart_ = nullptr;
    QChartView *tunnelChart_ = nullptr;
    QTableWidget *recent_ = nullptr;
    QTimer *chartTimer_ = nullptr;
};

} // namespace gui
