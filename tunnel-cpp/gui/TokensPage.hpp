#pragma once

#include <QWidget>

class QLineEdit;
class QTableWidget;

namespace gui {

class AppState;

class TokensPage : public QWidget {
    Q_OBJECT
public:
    explicit TokensPage(AppState *st, QWidget *parent = nullptr);
    void refresh();

private:
    AppState *st_;
    QLineEdit *name_;
    QTableWidget *table_;
};

} // namespace gui
