#pragma once

#include <QWidget>

class QCheckBox;
class QComboBox;
class QLineEdit;
class QTextEdit;

namespace gui {

class AppState;

class LogsPage : public QWidget {
    Q_OBJECT
public:
    explicit LogsPage(AppState *st, QWidget *parent = nullptr);
    void refresh();

private:
    AppState *st_;
    QComboBox *level_;
    QLineEdit *search_;
    QTextEdit *view_;
    QCheckBox *autoScroll_;
};

} // namespace gui
