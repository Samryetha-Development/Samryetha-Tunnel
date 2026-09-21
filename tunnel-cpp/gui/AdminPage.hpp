#pragma once

#include <QWidget>

class QLabel;
class QTableWidget;

namespace gui {

class AppState;

class AdminPage : public QWidget {
    Q_OBJECT
public:
    explicit AdminPage(AppState *st, QWidget *parent = nullptr);
    void refresh();

private:
    AppState *st_;
    QLabel *vUsers_ = nullptr;
    QLabel *vTunnels_ = nullptr;
    QLabel *vOnline_ = nullptr;
    QLabel *vReqs_ = nullptr;
    QTableWidget *users_;
    QTableWidget *tunnels_;
    QTableWidget *audit_;
};

} // namespace gui
