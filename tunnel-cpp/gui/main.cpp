#include "AppState.hpp"
#include "MainWindow.hpp"
#include "Theme.hpp"

#include <QApplication>
#include <QColor>
#include <QPalette>
#include <QStyleFactory>

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("Samryetha Tunnel"));
    QApplication::setOrganizationName(QStringLiteral("Samryetha"));
    QApplication::setApplicationVersion(QStringLiteral("0.3.0"));

    app.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    QPalette p;
    p.setColor(QPalette::Window, QColor("#0b0e14"));
    p.setColor(QPalette::WindowText, QColor("#e6e9ef"));
    p.setColor(QPalette::Base, QColor("#0f141d"));
    p.setColor(QPalette::AlternateBase, QColor("#121722"));
    p.setColor(QPalette::Text, QColor("#e6e9ef"));
    p.setColor(QPalette::Button, QColor("#1b2432"));
    p.setColor(QPalette::ButtonText, QColor("#e6e9ef"));
    p.setColor(QPalette::Highlight, QColor("#3ba55d"));
    p.setColor(QPalette::HighlightedText, QColor("#06240f"));
    p.setColor(QPalette::ToolTipBase, QColor("#141a24"));
    p.setColor(QPalette::ToolTipText, QColor("#e6e9ef"));
    p.setColor(QPalette::Disabled, QPalette::Text, QColor("#5b6675"));
    app.setPalette(p);
    app.setStyleSheet(theme::appStyleSheet());

    gui::AppState state;
    state.load();
    qInfo("config file: %s", state.configPath().toUtf8().constData());

    gui::MainWindow window(&state);
    window.show();

    return app.exec();
}
