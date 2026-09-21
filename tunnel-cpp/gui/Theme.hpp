#pragma once

#include <QString>

namespace theme {

// 参考 samryetha.com 的极简暗色：近黑底、无渐变、细分割线、排版驱动
inline const char *kText = "#ededed";
inline const char *kMuted = "#8f8f8f";
inline const char *kFaint = "#5c5c5c";
inline const char *kLine = "#1f1f1f";
inline const char *kOnline = "#3ddc84";

inline QString appStyleSheet() {
    return QStringLiteral(R"QSS(
* { font-family: "SF Pro Text", "Segoe UI", "Microsoft YaHei", sans-serif; font-size: 13px; outline: none; }
QMainWindow, QDialog, QWidget { background: #0a0a0a; color: #ededed; }

/* 排版层级 */
QLabel#eyebrow { color: #8f8f8f; font-size: 11px; font-weight: 600; letter-spacing: 1.2px; }
QLabel#pageTitle { font-size: 30px; font-weight: 700; letter-spacing: -0.6px; }
QLabel#sectionTitle { font-size: 15px; font-weight: 600; }
QLabel#muted { color: #8f8f8f; }
QLabel#faint { color: #5c5c5c; font-size: 11px; }
QLabel#mono { font-family: ui-monospace, "SF Mono", "JetBrains Mono", Consolas, monospace; color: #b9b9b9; }
QLabel#metricValue { font-size: 30px; font-weight: 600; letter-spacing: -0.5px; }
QLabel#metricLabel { color: #8f8f8f; font-size: 12px; }

/* 词语标记 */
QLabel#wordmark { font-size: 17px; font-weight: 700; letter-spacing: -0.3px; }

/* 侧栏 */
QWidget#sidebar { background: #0a0a0a; border-right: 1px solid #1a1a1a; }
QListWidget#nav { background: transparent; border: 0; outline: 0; padding: 0; }
QListWidget#nav::item { padding: 8px 12px; border-radius: 8px; color: #8f8f8f; margin: 1px 0; }
QListWidget#nav::item:hover { background: #151515; color: #dcdcdc; }
QListWidget#nav::item:selected { background: #1b1b1b; color: #ffffff; }
QListWidget#rail { background: transparent; border: 0; outline: 0; }
QListWidget#rail::item { padding: 7px 2px; color: #8f8f8f; border-bottom: 1px solid #161616; }
QListWidget#rail::item:hover { color: #ededed; }

/* 按钮：主按钮=白底黑字胶囊（对齐你站上的 Post / Sign in） */
QPushButton {
    background: transparent; border: 1px solid #2a2a2a; border-radius: 909px;
    padding: 7px 16px; color: #dcdcdc; font-weight: 550;
}
QPushButton:hover { background: #161616; border-color: #3a3a3a; color: #ffffff; }
QPushButton:pressed { background: #101010; }
QPushButton:disabled { color: #4d4d4d; border-color: #1c1c1c; background: transparent; }
QPushButton#primary { background: #ffffff; color: #0a0a0a; border: 0; font-weight: 650; }
QPushButton#primary:hover { background: #e6e6e6; }
QPushButton#primary:disabled { background: #2a2a2a; color: #6a6a6a; }

/* 文本按钮（列表行内操作） */
QPushButton#link { background: transparent; border: 0; padding: 4px 8px; color: #8f8f8f; font-weight: 500; }
QPushButton#link:hover { color: #ffffff; background: transparent; }
QPushButton#linkDanger { background: transparent; border: 0; padding: 4px 8px; color: #8f8f8f; }
QPushButton#linkDanger:hover { color: #ff6b6b; background: transparent; }

/* 输入 */
QLineEdit, QPlainTextEdit, QTextEdit, QComboBox, QSpinBox {
    background: #101010; border: 1px solid #232323; border-radius: 9px;
    padding: 8px 11px; color: #ededed; selection-background-color: #ffffff; selection-color: #0a0a0a;
}
QLineEdit:focus, QPlainTextEdit:focus, QTextEdit:focus, QComboBox:focus, QSpinBox:focus { border-color: #4a4a4a; }
QLineEdit:hover, QComboBox:hover { border-color: #303030; }
QComboBox::drop-down { border: 0; width: 20px; }
QComboBox QAbstractItemView { background: #121212; border: 1px solid #232323; border-radius: 8px; padding: 4px;
    selection-background-color: #1e1e1e; selection-color: #ffffff; }

/* 列表 / 表格：无网格，细分割线 */
QTableWidget, QTableView { background: transparent; border: 0; gridline-color: transparent; }
QTableWidget::item { padding: 9px 6px; border-bottom: 1px solid #161616; color: #c9c9c9; }
QTableWidget::item:selected { background: #171717; color: #ffffff; }
QHeaderView::section { background: transparent; color: #8f8f8f; border: 0; border-bottom: 1px solid #1f1f1f;
    padding: 8px 6px; font-weight: 600; font-size: 11px; }
QTableCornerButton::section { background: transparent; border: 0; }

/* 分组 / Tab */
QGroupBox { background: transparent; border: 0; border-top: 1px solid #1f1f1f; margin-top: 20px; padding-top: 14px; }
QGroupBox::title { subcontrol-origin: margin; left: 0; top: 0; padding: 0 0 6px 0;
    color: #8f8f8f; font-size: 11px; font-weight: 600; letter-spacing: 1.2px; }
QTabWidget::pane { border: 0; border-top: 1px solid #1f1f1f; }
QTabBar::tab { background: transparent; padding: 8px 2px; margin-right: 22px; color: #8f8f8f; border-bottom: 1px solid transparent; }
QTabBar::tab:selected { color: #ffffff; border-bottom: 1px solid #ffffff; }
QTabBar::tab:hover { color: #dcdcdc; }

/* 滚动条 */
QScrollBar:vertical { background: transparent; width: 8px; margin: 0; }
QScrollBar::handle:vertical { background: #262626; border-radius: 4px; min-height: 40px; }
QScrollBar::handle:vertical:hover { background: #3a3a3a; }
QScrollBar::add-line, QScrollBar::sub-line { height: 0; }
QScrollBar:horizontal { background: transparent; height: 8px; }
QScrollBar::handle:horizontal { background: #262626; border-radius: 4px; min-width: 40px; }

QCheckBox { color: #c9c9c9; spacing: 8px; }
QCheckBox::indicator { width: 16px; height: 16px; border-radius: 4px; border: 1px solid #303030; background: #101010; }
QCheckBox::indicator:checked { background: #ffffff; border-color: #ffffff; }

QToolTip { background: #121212; color: #ededed; border: 1px solid #232323; border-radius: 6px; padding: 5px 8px; }

/* 行容器（仿你论坛列表行） */
QFrame#row { background: transparent; border-bottom: 1px solid #161616; }
QFrame#row:hover { background: #101010; }
QFrame#sep { background: #1f1f1f; max-height: 1px; border: 0; }
)QSS");
}

} // namespace theme
