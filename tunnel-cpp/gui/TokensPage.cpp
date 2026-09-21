#include "TokensPage.hpp"
#include "AppState.hpp"
#include "Ui.hpp"

#include "tunnel/Api.hpp"

#include <QClipboard>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

namespace gui {

TokensPage::TokensPage(AppState *st, QWidget *parent) : QWidget(parent), st_(st) {
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(34, 30, 34, 24);
    root->setSpacing(0);
    root->addWidget(ui::eyebrow(QStringLiteral("api tokens")));
    root->addWidget(ui::pageTitle(QStringLiteral("访问令牌")));
    root->addSpacing(18);

    auto *bar = new QHBoxLayout;
    name_ = new QLineEdit;
    name_->setPlaceholderText(QStringLiteral("令牌名称，如 macbook"));
    auto *create = new QPushButton(QStringLiteral("新建令牌"));
    create->setObjectName("primary");
    auto *reload = new QPushButton(QStringLiteral("刷新"));
    reload->setObjectName("ghost");
    bar->addWidget(name_, 1);
    bar->addWidget(create);
    bar->addWidget(reload);
    bar->addStretch();
    root->addLayout(bar);
    root->addSpacing(14);

    table_ = new QTableWidget;
    table_->setColumnCount(5);
    table_->setHorizontalHeaderLabels({QStringLiteral("名称"), QStringLiteral("前缀"),
                                       QStringLiteral("创建时间"), QStringLiteral("最近使用"),
                                       QStringLiteral("操作")});
    table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    table_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    table_->verticalHeader()->setVisible(false);
    table_->setShowGrid(false);
    table_->setAlternatingRowColors(true);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    root->addWidget(table_, 1);

    connect(reload, &QPushButton::clicked, this, &TokensPage::refresh);
    connect(create, &QPushButton::clicked, this, [this]() {
        const QString n = name_->text().trimmed().isEmpty() ? QStringLiteral("default")
                                                            : name_->text().trimmed();
        QJsonObject body;
        body["name"] = n;
        st_->mgmt("POST",
                            "/api/tokens", body,
                            [this](bool ok, const QJsonObject &o, const QString &e) {
                                if (!ok) {
                                    QMessageBox::warning(this, QStringLiteral("失败"), e);
                                    return;
                                }
                                const QString tok = o.value("token").toString();
                                QGuiApplication::clipboard()->setText(tok);
                                QMessageBox::information(
                                    this, QStringLiteral("令牌已创建（已复制到剪贴板）"),
                                    QStringLiteral("这是唯一一次显示明文，请妥善保存：\n\n%1").arg(tok));
                                refresh();
                            });
    });

    connect(st_, &AppState::meChanged, this, &TokensPage::refresh);
    refresh();
}

void TokensPage::refresh() {
    if (st_->config().apiToken.isEmpty()) {
        table_->setRowCount(0);
        return;
    }
    st_->mgmt("GET",
                        "/api/tokens", {},
                        [this](bool ok, const QJsonObject &o, const QString &e) {
                            if (!ok) {
                                table_->setRowCount(0);
                                Q_UNUSED(e);
                                return;
                            }
                            const QJsonArray arr = o.value("tokens").toArray();
                            table_->setRowCount(arr.size());
                            for (int i = 0; i < arr.size(); ++i) {
                                const auto t = arr[i].toObject();
                                table_->setItem(i, 0, new QTableWidgetItem(t.value("name").toString()));
                                table_->setItem(i, 1, new QTableWidgetItem(t.value("prefix").toString() + "…"));
                                table_->setItem(i, 2, new QTableWidgetItem(t.value("created_at").toString().left(19)));
                                table_->setItem(i, 3, new QTableWidgetItem(t.value("last_used_at").toString().left(19)));
                                const int id = t.value("id").toInt();
                                auto *del = new QPushButton(QStringLiteral("吊销"));
                                del->setObjectName("linkDanger");
                                connect(del, &QPushButton::clicked, this, [this, id]() {
                                    if (QMessageBox::question(this, QStringLiteral("吊销"),
                                                              QStringLiteral("确定吊销该令牌？"))
                                        != QMessageBox::Yes)
                                        return;
                                    st_->mgmt("DELETE",
                                                        QStringLiteral("/api/tokens/%1").arg(id), {},
                                                        [this](bool ok2, const QJsonObject &, const QString &e2) {
                                                            if (!ok2)
                                                                QMessageBox::warning(this, QStringLiteral("失败"), e2);
                                                            refresh();
                                                        });
                                });
                                table_->setCellWidget(i, 4, del);
                            }
                        });
}

} // namespace gui
