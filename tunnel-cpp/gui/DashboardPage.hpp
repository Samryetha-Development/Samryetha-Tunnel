#pragma once

#include <QWidget>

class QLabel;
class QPushButton;

namespace gui {

class AppState;

class DashboardPage : public QWidget {
    Q_OBJECT
public:
    explicit DashboardPage(AppState *st, QWidget *parent = nullptr);
    void refresh();

private:
    AppState *st_;
    QLabel *dot_ = nullptr;
    QLabel *title_ = nullptr;
    QLabel *subtitle_ = nullptr;
    QLabel *vTunnels_ = nullptr;
    QLabel *vReqs_ = nullptr;
    QLabel *vRate_ = nullptr;
    QLabel *vBytes_ = nullptr;
    QWidget *cardsHost_ = nullptr;
    QPushButton *connectBtn_ = nullptr;

    void rebuildCards(bool connected);
    QWidget *buildTunnelCard(int index, bool connected);
};

} // namespace gui
