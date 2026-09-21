#include "LogsPage.hpp"
#include "AppState.hpp"
#include "Ui.hpp"

#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QFileDialog>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QTextEdit>
#include <QTextStream>
#include <QVBoxLayout>

namespace gui {

static QString levelColor(const QString &l) {
    if (l == "ok") return "#3ddc84";
    if (l == "warn") return "#f5b74e";
    if (l == "err") return "#ff5d6c";
    return "#7d8ba1";
}

static QString escape(const QString &s) {
    QString o = s;
    o.replace('&', "&amp;").replace('<', "&lt;").replace('>', "&gt;");
    return o;
}

LogsPage::LogsPage(AppState *st, QWidget *parent) : QWidget(parent), st_(st) {
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(20, 20, 20, 20);
    root->setSpacing(12);

    root->addWidget(ui::sectionTitle(QStringLiteral("运行日志")));

    auto *bar = new QHBoxLayout;
    bar->setSpacing(8);
    level_ = new QComboBox;
    level_->addItems({QStringLiteral("全部"), QStringLiteral("info"), QStringLiteral("ok"),
                      QStringLiteral("warn"), QStringLiteral("err")});
    search_ = new QLineEdit;
    search_->setPlaceholderText(QStringLiteral("搜索日志…"));
    autoScroll_ = new QCheckBox(QStringLiteral("自动滚"));
    autoScroll_->setChecked(true);
    auto *copyBtn = new QPushButton(QStringLiteral("复制"));
    copyBtn->setObjectName("ghost");
    auto *exportBtn = new QPushButton(QStringLiteral("导出"));
    exportBtn->setObjectName("ghost");
    auto *clearBtn = new QPushButton(QStringLiteral("清空"));
    clearBtn->setObjectName("danger");
    bar->addWidget(level_);
    bar->addWidget(search_, 1);
    bar->addWidget(autoScroll_);
    bar->addWidget(copyBtn);
    bar->addWidget(exportBtn);
    bar->addWidget(clearBtn);
    root->addLayout(bar);

    view_ = new QTextEdit;
    view_->setReadOnly(true);
    view_->setStyleSheet(QStringLiteral("font-family: ui-monospace, Menlo, Consolas, monospace;"
                                        "font-size: 12px; background:#0d1420; border:1px solid #1f2937;"
                                        "border-radius:12px; padding:8px;"));
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
        if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
            QTextStream(&f) << view_->toPlainText();
    });
    refresh();
}

void LogsPage::refresh() {
    const QString lv = level_->currentText();
    const QString q = search_->text();
    QString html;
    html += QStringLiteral("<div style='line-height:1.5'>");
    for (const auto &e : st_->logs()) {
        if (lv != QStringLiteral("全部") && e.level != lv)
            continue;
        if (!q.isEmpty() && !e.message.contains(q, Qt::CaseInsensitive) &&
            !e.source.contains(q, Qt::CaseInsensitive))
            continue;
        const QString color = levelColor(e.level);
        html += QStringLiteral(
                    "<span style='color:#4a5568'>%1</span> "
                    "<span style='color:%2;font-weight:700'>[%3]</span> "
                    "<span style='color:#8aa0b8'>%4</span> "
                    "<span style='color:#dbe4ef'>%5</span><br>")
                    .arg(e.at.toString(QStringLiteral("HH:mm:ss")), color, e.level,
                         escape(e.source), escape(e.message));
    }
    html += QStringLiteral("</div>");
    view_->setHtml(html);
    if (autoScroll_->isChecked()) {
        auto *sb = view_->verticalScrollBar();
        sb->setValue(sb->maximum());
    }
}

} // namespace gui
