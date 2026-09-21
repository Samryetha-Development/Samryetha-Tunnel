#pragma once

#include <QWidget>

class QCheckBox;
class QLabel;
class QLineEdit;
class QTableWidget;
class QTimer;

namespace tunnel {
class Api;
}

namespace gui {

class AppState;

class SettingsPage : public QWidget {
    Q_OBJECT
public:
    explicit SettingsPage(AppState *st, QWidget *parent = nullptr);
    void refresh();

private:
    AppState *st_;
    QLineEdit *server_;
    QLineEdit *token_;
    QLineEdit *clientId_;
    QLineEdit *base_;
    QCheckBox *autoReconnect_;
    QLabel *authStatus_;
    QTableWidget *tunnelTable_;
    tunnel::Api *api_;
    QTimer *poll_;
    QString deviceCode_;
    void applyToConfig();
};

} // namespace gui
