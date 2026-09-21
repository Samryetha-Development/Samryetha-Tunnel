#include "UsagePage.hpp"
#include "AppState.hpp"
#include "Ui.hpp"

#include "tunnel/Api.hpp"

#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QVBoxLayout>

namespace gui {

static QString fmtB(double b) {
    if (b < 1024) return QStringLiteral("%1 B").arg(b, 0, 'f', 0);
    if (b < 1024 * 1024) return QStringLiteral("%1 KB").arg(b / 1024, 0, 'f', 1);
    if (b < 1024.0 * 1024 * 1024) return QStringLiteral("%1 MB").arg(b / 1024 / 1024, 0, 'f', 1);
    return QStringLiteral("%1 GB").arg(b / 1024 / 1024 / 1024, 0, 'f', 2);
}

UsagePage::UsagePage(AppState *st, QWidget *parent) : QWidget(parent), st_(st) {
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(34, 30, 34, 24);
    root->setSpacing(0);
    root->addWidget(ui::eyebrow(QStringLiteral("usage")));
    root->addWidget(ui::pageTitle(QStringLiteral("用量")));
    root->addSpacing(20);

    auto *metrics = new QHBoxLayout;
    metrics->setSpacing(60);
    metrics->addWidget(ui::metric(QStringLiteral("今日流量"), &vBytes_));
    metrics->addWidget(ui::metric(QStringLiteral("每日配额"), &vLimit_));
    metrics->addWidget(ui::metric(QStringLiteral("今日请求"), &vRequests_));
    metrics->addWidget(ui::metric(QStringLiteral("日期"), &vDay_));
    metrics->addStretch();
    root->addLayout(metrics);
    root->addSpacing(22);

    bar_ = new QProgressBar;
    bar_->setRange(0, 100);
    bar_->setTextVisible(true);
    root->addWidget(bar_);
    root->addSpacing(16);

    auto *reload = new QPushButton(QStringLiteral("刷新"));
    reload->setObjectName("ghost");
    connect(reload, &QPushButton::clicked, this, &UsagePage::refresh);
    root->addWidget(reload, 0, Qt::AlignLeft);
    root->addStretch();

    connect(st_, &AppState::meChanged, this, &UsagePage::refresh);
    refresh();
}

void UsagePage::refresh() {
    if (st_->config().apiToken.isEmpty())
        return;
    st_->mgmt("GET",
                        "/api/usage", {},
                        [this](bool ok, const QJsonObject &o, const QString &) {
                            if (!ok)
                                return;
                            const double bytes = o.value("bytes").toDouble();
                            const double limit = o.value("daily_bytes_limit").toDouble();
                            vBytes_->setText(fmtB(bytes));
                            vLimit_->setText(fmtB(limit));
                            vRequests_->setText(QString::number(o.value("requests").toInt()));
                            vDay_->setText(o.value("day").toString());
                            const int pct = limit > 0 ? int(bytes / limit * 100) : 0;
                            bar_->setValue(qBound(0, pct, 100));
                            bar_->setFormat(QStringLiteral("%1%").arg(pct));
                        });
}

} // namespace gui
