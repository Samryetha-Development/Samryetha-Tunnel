#pragma once

#include <QWidget>

class QLabel;
class QProgressBar;

namespace gui {

class AppState;

class UsagePage : public QWidget {
    Q_OBJECT
public:
    explicit UsagePage(AppState *st, QWidget *parent = nullptr);
    void refresh();

private:
    AppState *st_;
    QLabel *vBytes_ = nullptr;
    QLabel *vLimit_ = nullptr;
    QLabel *vRequests_ = nullptr;
    QLabel *vDay_ = nullptr;
    QProgressBar *bar_ = nullptr;
};

} // namespace gui
