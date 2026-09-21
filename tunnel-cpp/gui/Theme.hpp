#pragma once

#include <QString>

namespace theme {

// 现代深色配色
inline const char *kAccent = "#3ddc84";     // 主强调（绿）
inline const char *kAccent2 = "#4c8dff";    // 次强调（蓝）
inline const char *kPurple = "#a78bfa";
inline const char *kDanger = "#ff5d6c";
inline const char *kWarn = "#f5b74e";
inline const char *kMuted = "#7d8ba1";

inline QString appStyleSheet() {
    return QStringLiteral(R"QSS(
* { font-family: "SF Pro Text", "Segoe UI", "Microsoft YaHei", sans-serif; font-size: 13px; outline: none; }
QMainWindow, QDialog { background: #0a0d13; }
QWidget { color: #e8edf5; }

QLabel#muted { color: #7d8ba1; }
QLabel#mono { font-family: ui-monospace, "SF Mono", "JetBrains Mono", Consolas, monospace; }
QLabel#h1 { font-size: 20px; font-weight: 700; }
QLabel#h2 { font-size: 15px; font-weight: 650; }
QLabel#cardLabel { color: #7d8ba1; font-size: 11px; letter-spacing: .3px; }
QLabel#cardValue { font-size: 26px; font-weight: 700; font-family: ui-monospace, "SF Mono", Consolas, monospace; }

/* ---------- 品牌 ---------- */
QLabel#brandMark {
    font-size: 17px; font-weight: 800; color: #06240f;
    background: qlineargradient(x1:0,y1:0,x2:1,y2:1, stop:0 #3ddc84, stop:1 #22b06a);
    border-radius: 9px; padding: 6px 10px;
}
QLabel#brandName { font-size: 15px; font-weight: 700; }

/* ---------- Hero 头图 ---------- */
QFrame#hero {
    border-radius: 14px;
    background: qlineargradient(x1:0,y1:0,x2:1,y2:1,
                stop:0 #0f3d2a, stop:0.45 #0e2f3f, stop:1 #141a35);
    border: 1px solid #1e3a46;
}
QFrame#hero QLabel#h1, QFrame#hero QLabel { color: #f2f7fb; }
QFrame#hero QLabel#heroSub { color: #9fc3b6; font-family: ui-monospace, Consolas, monospace; }

/* ---------- 卡片 ---------- */
QFrame#card, QFrame#tile, QFrame#tunnelCard {
    background: qlineargradient(x1:0,y1:0,x2:0,y2:1, stop:0 #131a28, stop:1 #101725);
    border: 1px solid #1f2937;
    border-radius: 12px;
}
QFrame#tunnelCard:hover { border: 1px solid #2c4a5e; background: #15202f; }
QFrame#tile { border-radius: 12px; }

/* ---------- 侧栏 ---------- */
QWidget#sidebar { background: #0c111b; border-right: 1px solid #18202e; }
QListWidget#nav { background: transparent; border: 0; outline: 0; padding: 4px; }
QListWidget#nav::item { padding: 9px 12px; border-radius: 9px; color: #93a1b5; margin: 2px 4px; }
QListWidget#nav::item:hover { background: #141d2b; color: #dbe4ef; }
QListWidget#nav::item:selected {
    background: qlineargradient(x1:0,y1:0,x2:1,y2:0, stop:0 #14342a, stop:1 #12283a);
    color: #eafff2; border-left: 3px solid #3ddc84;
}
QListWidget#tunnelList { background: transparent; border: 0; outline: 0; }
QListWidget#tunnelList::item { padding: 5px 10px; border-radius: 7px; color: #aab6c6; }
QListWidget#tunnelList::item:hover { background: #141d2b; }

/* ---------- 按钮 ---------- */
QPushButton {
    background: #182130; border: 1px solid #263349; border-radius: 9px;
    padding: 7px 14px; color: #e8edf5; font-weight: 550;
}
QPushButton:hover { background: #1f2b3d; border-color: #33465f; }
QPushButton:pressed { background: #16202e; }
QPushButton:disabled { color: #55617a; background: #111823; border-color: #1b2434; }

QPushButton#primary {
    background: qlineargradient(x1:0,y1:0,x2:1,y2:1, stop:0 #3ddc84, stop:1 #24b46b);
    color: #05270f; border: 0; font-weight: 750;
}
QPushButton#primary:hover { background: qlineargradient(x1:0,y1:0,x2:1,y2:1, stop:0 #4ae792, stop:1 #2ac277); }
QPushButton#primary:disabled { background: #1c3a2a; color: #4d7a63; }

QPushButton#danger { color: #ff8a95; border-color: #4a2630; background: #1c1418; }
QPushButton#danger:hover { background: #2a171c; border-color: #6b3040; }

QPushButton#ghost { background: transparent; border: 1px solid #263349; }
QPushButton#ghost:hover { background: #141d2b; }

QPushButton#iconBtn { padding: 6px 10px; border-radius: 8px; }

/* ---------- 输入 ---------- */
QLineEdit, QPlainTextEdit, QTextEdit, QComboBox, QSpinBox {
    background: #0d1420; border: 1px solid #263349; border-radius: 9px;
    padding: 7px 10px; color: #e8edf5; selection-background-color: #3ddc84; selection-color: #05270f;
}
QLineEdit:hover, QComboBox:hover { border-color: #33465f; }
QLineEdit:focus, QPlainTextEdit:focus, QTextEdit:focus, QComboBox:focus, QSpinBox:focus {
    border: 1px solid #3ddc84;
}
QComboBox::drop-down { border: 0; width: 22px; }
QComboBox QAbstractItemView {
    background: #0f1622; border: 1px solid #263349; border-radius: 8px;
    selection-background-color: #1b3b2c; selection-color: #eafff2; padding: 4px;
}

/* ---------- 表格 ---------- */
QTableWidget, QTableView {
    background: transparent; border: 0; gridline-color: transparent;
    alternate-background-color: #0f1622;
}
QTableWidget::item { padding: 7px 6px; border-bottom: 1px solid #161f2d; }
QTableWidget::item:selected { background: #14342a; color: #eafff2; }
QHeaderView::section {
    background: transparent; color: #7d8ba1; border: 0; border-bottom: 1px solid #1f2937;
    padding: 7px 6px; font-weight: 600; font-size: 11px;
}
QTableCornerButton::section { background: transparent; border: 0; }

/* ---------- 分组 / 标签页 ---------- */
QGroupBox {
    background: qlineargradient(x1:0,y1:0,x2:0,y2:1, stop:0 #131a28, stop:1 #101725);
    border: 1px solid #1f2937; border-radius: 12px; margin-top: 16px; padding: 14px 12px 10px 12px;
}
QGroupBox::title {
    subcontrol-origin: margin; left: 12px; top: 2px; padding: 2px 8px;
    color: #3ddc84; font-weight: 700; font-size: 11px; letter-spacing: .4px;
}
QTabWidget::pane { border: 1px solid #1f2937; border-radius: 10px; top: -1px; }
QTabBar::tab { background: transparent; padding: 7px 14px; color: #7d8ba1; border-radius: 8px; margin-right: 4px; }
QTabBar::tab:selected { color: #eafff2; background: #14342a; }

/* ---------- 滚动条 ---------- */
QScrollBar:vertical { background: transparent; width: 10px; margin: 2px; }
QScrollBar::handle:vertical { background: #24314a; border-radius: 5px; min-height: 30px; }
QScrollBar::handle:vertical:hover { background: #33465f; }
QScrollBar::add-line, QScrollBar::sub-line { height: 0; }
QScrollBar:horizontal { background: transparent; height: 10px; margin: 2px; }
QScrollBar::handle:horizontal { background: #24314a; border-radius: 5px; min-width: 30px; }

QCheckBox { color: #c6d1e0; spacing: 7px; }
QCheckBox::indicator { width: 17px; height: 17px; border-radius: 5px; border: 1px solid #2c3a52; background: #0d1420; }
QCheckBox::indicator:checked { background: #3ddc84; border-color: #3ddc84; }

QToolTip { background: #0f1622; color: #e8edf5; border: 1px solid #263349; border-radius: 6px; padding: 5px 8px; }
QStatusBar { background: #0c111b; color: #7d8ba1; }

/* ---------- 胶囊 ---------- */
QLabel#pillOk { background: rgba(61,220,132,.16); color: #3ddc84; border-radius: 9px; padding: 2px 10px; font-weight: 650; font-size: 11px; }
QLabel#pillOff { background: rgba(125,139,161,.16); color: #93a1b5; border-radius: 9px; padding: 2px 10px; font-size: 11px; }
QLabel#pillErr { background: rgba(255,93,108,.16); color: #ff8a95; border-radius: 9px; padding: 2px 10px; font-weight: 650; font-size: 11px; }
QLabel#chip { background: #141d2b; color: #93a1b5; border: 1px solid #22304a; border-radius: 8px; padding: 2px 8px; font-size: 11px; }
)QSS");
}

} // namespace theme
