#pragma once

#include <QString>

namespace theme {

inline QString appStyleSheet() {
    return QStringLiteral(R"QSS(
* { font-family: -apple-system, "Segoe UI", "Microsoft YaHei", sans-serif; font-size: 13px; }
QMainWindow, QDialog { background: #0b0e14; }
QWidget { color: #e6e9ef; }
QLabel#cardValue { font-size: 22px; font-weight: 600; }
QLabel#cardLabel { color: #8b98a9; font-size: 11px; }
QLabel#muted { color: #8b98a9; }
QLabel#mono, QLabel#cardValue { font-family: ui-monospace, "SF Mono", Consolas, monospace; }
QFrame#card, QFrame#tile {
    background: #121722; border: 1px solid #222b38; border-radius: 10px;
}
QListWidget#sidebar {
    background: #0e131c; border: 0; border-right: 1px solid #1b2330; outline: 0; padding: 6px;
}
QListWidget#sidebar::item { padding: 8px 10px; border-radius: 7px; color: #aab4c2; }
QListWidget#sidebar::item:selected { background: #1b2432; color: #ffffff; }
QListWidget#sidebar::item:hover { background: #161e2a; }
QPushButton {
    background: #1b2432; border: 1px solid #2a3546; border-radius: 8px; padding: 6px 12px; color: #e6e9ef;
}
QPushButton:hover { background: #223041; }
QPushButton:disabled { color: #5b6675; background: #141a24; }
QPushButton#primary { background: #3ba55d; color: #06240f; border: 0; font-weight: 600; }
QPushButton#primary:hover { background: #46b869; }
QPushButton#danger { color: #ff6b6b; border-color: #4a2626; }
QPushButton#danger:hover { background: #2a1717; }
QLineEdit, QPlainTextEdit, QComboBox, QSpinBox {
    background: #0f141d; border: 1px solid #2a3546; border-radius: 8px; padding: 6px 8px; color: #e6e9ef;
    selection-background-color: #3ba55d;
}
QLineEdit:focus, QPlainTextEdit:focus, QComboBox:focus { border-color: #3ba55d; }
QHeaderView::section {
    background: #141a24; color: #8b98a9; border: 0; border-bottom: 1px solid #222b38; padding: 6px;
}
QTableWidget { background: #0f141d; border: 1px solid #222b38; border-radius: 8px; gridline-color: #1a2230; }
QTableWidget::item:selected { background: #1b2432; }
QTabWidget::pane { border: 1px solid #222b38; border-radius: 8px; }
QTabBar::tab { background: transparent; padding: 6px 12px; color: #8b98a9; }
QTabBar::tab:selected { color: #e6e9ef; border-bottom: 2px solid #3ba55d; }
QCheckBox { color: #c7d0da; }
QScrollBar:vertical { background: transparent; width: 10px; }
QScrollBar::handle:vertical { background: #2a3546; border-radius: 5px; min-height: 24px; }
QToolTip { background: #141a24; color: #e6e9ef; border: 1px solid #2a3546; }
)QSS");
}

inline QString badge(bool ok) {
    return ok ? QStringLiteral("background:#16351f;color:#54d98c;border-radius:9px;padding:2px 8px;")
              : QStringLiteral("background:#242a33;color:#8b98a9;border-radius:9px;padding:2px 8px;");
}

} // namespace theme
