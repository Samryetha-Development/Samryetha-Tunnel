#include "LogsPage.hpp"
#include "AppState.hpp"

#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QFileDialog>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QTextStream>
#include <QVBoxLayout>

namespace gui {

LogsPage::LogsPage(AppState *st, QWidget *parent) : QWidget(parent), st_(st) {
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(18, 18, 18, 18);
    root->setSpacing(10);

    auto *bar = new QHBoxLayout;
    level_ = new QComboBox;
    level_->addItems({QStringLiteral("全部"), QStringLiteral("info"), QStringLiteral("ok"),
                      QStringLiteral("warn"), QStringLiteral("err")});
    search_ = new QLineEdit;
    search_->setPlaceholderText(QStringLiteral("搜索日志"));
    autoScroll_ = new QCheckBox(QStringLiteral("自动滚"));
    autoScroll_->setChecked(true);
    auto *copyBtn = new QPushButton(QStringLiteral("复制"));
    auto *exportBtn = new QPushButton(QStringLiteral("导出"));
    auto *clearBtn = new QPushButton(QStringLiteral("清空"));
    bar->addWidget(level_);
    bar->addWidget(search_, 1);
    bar->addWidget(autoScroll_);
    bar->addWidget(copyBtn);
    bar->addWidget(exportBtn);
    bar->addWidget(clearBtn);
    root->addLayout(bar);

    view_ = new QPlainTextEdit;
    view_->setReadOnly(true);
    view_->setStyleSheet(QStringLiteral("font-family: ui-monospace, Menlo, Consolas, monospace;"));
    root->addWidget(view_, 1);

    connect(st_, &AppState::logsChanged, this, &LogsPage::refresh);
    connect(level_, &QComboBox::currentTextChanged, this, &LogsPage::refresh);
    connect(search_, &QLineEdit::textChanged, this, &LogsPage::refresh);
    connect(clearBtn, &QPushButton::clicked, st_, &AppState::clearLogs);
    connect(copyBtn, &QPushButton::clicked, this, [this]() {
        QGuiApplication::clipboard()->setText(view_->toPlainText());
    });
    connect(exportBtn, &QPushButton::clicked, this, [this]() {
        const QString path = QFileDialog::getSaveFileName(
            this, QStringLiteral("导出日志"), QStringLiteral("tunnel.log"));
        if (path.isEmpty())
            return;
        QFile f(path);
        if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            QTextStream(&f) << view_->toPlainText();
        }
    });
    refresh();
}

void LogsPage::refresh() {
    const QString lv = level_->currentText();
    const QString q = search_->text();
    QString out;
    for (const auto &e : st_->logs()) {
        if (lv != QStringLiteral("全部") && e.level != lv)
            continue;
        if (!q.isEmpty() && !e.message.contains(q, Qt::CaseInsensitive) &&
            !e.source.contains(q, Qt::CaseInsensitive))
            continue;
        out += QStringLiteral("[%1][%2][%3] %4\n")
                   .arg(e.at.toString(QStringLiteral("HH:mm:ss")), e.level, e.source, e.message);
    }
    view_->setPlainText(out);
    if (autoScroll_->isChecked()) {
        auto *sb = view_->verticalScrollBar();
        sb->setValue(sb->maximum());
    }
}

} // namespace gui
