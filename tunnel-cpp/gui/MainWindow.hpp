#pragma once

#include <QMainWindow>

class QLabel;
class QListWidget;
class QStackedWidget;
class QTimer;

namespace gui {

class AppState;
class DashboardPage;
class LogsPage;
class StatsPage;
class SettingsPage;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(AppState *st, QWidget *parent = nullptr);

private slots:
    void refreshStatus();

private:
    AppState *st_;
    QLabel *statusTitle_;
    QLabel *statusSub_;
    QLabel *statusDot_;
    QListWidget *tunnelList_;
    QStackedWidget *stack_;
    QTimer *tick_;
};

} // namespace gui
