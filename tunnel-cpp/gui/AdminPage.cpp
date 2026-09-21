#include "AdminPage.hpp"
#include "AppState.hpp"
#include "Ui.hpp"

#include "tunnel/Api.hpp"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

namespace gui {

AdminPage::AdminPage(AppState *st, QWidget *parent) : QWidget(parent), st_(st) {
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(34, 30, 34, 24);
    root->setSpacing(0);
    root->addWidget(ui::eyebrow(QStringLiteral("admin")));
    root->addWidget(ui::pageTitle(QStringLiteral("管理")));
    root->addSpacing(18);

    auto *metrics = new QHBoxLayout;
    metrics->setSpacing(60);
    metrics->addWidget(ui::metric(QStringLiteral("用户"), &vUsers_));
    metrics->addWidget(ui::metric(QStringLiteral("隧道"), &vTunnels_));
    metrics->addWidget(ui::metric(QStringLiteral("在线客户端"), &vOnline_));
    metrics->addWidget(ui::metric(QStringLiteral("今日请求"), &vReqs_));
    metrics->addStretch();
    root->addLayout(metrics);
    root->addSpacing(20);

    auto *reload = new QPushButton(QStringLiteral("刷新"));
    reload->setObjectName("ghost");
    connect(reload, &QPushButton::clicked, this, &AdminPage::refresh);
    root->addWidget(reload, 0, Qt::AlignLeft);
    root->addSpacing(14);

    root->addWidget(ui::eyebrow(QStringLiteral("users")));
    users_ = new QTableWidget;
    users_->setColumnCount(6);
    users_->setHorizontalHeaderLabels({QStringLiteral("邮箱"), QStringLiteral("命名空间"),
                                       QStringLiteral("角色"), QStringLiteral("隧道配额"),
                                       QStringLiteral("状态"), QStringLiteral("操作")});
    users_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    users_->verticalHeader()->setVisible(false);
    users_->setShowGrid(false);
    users_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    users_->setMinimumHeight(180);
    root->addWidget(users_);
    root->addSpacing(16);

    root->addWidget(ui::eyebrow(QStringLiteral("tunnels")));
    tunnels_ = new QTableWidget;
    tunnels_->setColumnCount(4);
    tunnels_->setHorizontalHeaderLabels({QStringLiteral("用户"), QStringLiteral("隧道"),
                                         QStringLiteral("公网地址"), QStringLiteral("操作")});
    tunnels_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    tunnels_->verticalHeader()->setVisible(false);
    tunnels_->setShowGrid(false);
    tunnels_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    tunnels_->setMinimumHeight(140);
    root->addWidget(tunnels_);
    root->addSpacing(16);

    root->addWidget(ui::eyebrow(QStringLiteral("audit")));
    audit_ = new QTableWidget;
    audit_->setColumnCount(4);
    audit_->setHorizontalHeaderLabels({QStringLiteral("时间"), QStringLiteral("操作者"),
                                       QStringLiteral("动作"), QStringLiteral("详情")});
    audit_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    audit_->verticalHeader()->setVisible(false);
    audit_->setShowGrid(false);
    audit_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    audit_->setMinimumHeight(140);
    root->addWidget(audit_);
    root->addStretch();

    connect(st_, &AppState::meChanged, this, &AdminPage::refresh);
    refresh();
}

void AdminPage::refresh() {
    if (!st_->isAdmin())
        return;
    const QString base = st_->config().effectiveApiBase();
    const QString tok = st_->config().apiToken;

    st_->api()->request(base, tok, "GET", "/api/admin/overview", {},
                        [this](bool ok, const QJsonObject &o, const QString &) {
                            if (!ok) return;
                            vUsers_->setText(QString::number(o.value("users").toInt()));
                            vTunnels_->setText(QString::number(o.value("tunnels").toInt()));
                            vOnline_->setText(QString::number(o.value("online_clients").toInt()));
                            vReqs_->setText(QString::number(o.value("requests_today").toInt()));
                        });

    st_->api()->request(base, tok, "GET", "/api/admin/users", {},
                        [this, base, tok](bool ok, const QJsonObject &o, const QString &) {
                            if (!ok) return;
                            const auto arr = o.value("users").toArray();
                            users_->setRowCount(arr.size());
                            for (int i = 0; i < arr.size(); ++i) {
                                const auto u = arr[i].toObject();
                                const int id = u.value("id").toInt();
                                const bool disabled = u.value("disabled").toBool();
                                const QString role = u.value("role").toString();
                                users_->setItem(i, 0, new QTableWidgetItem(u.value("email").toString()));
                                users_->setItem(i, 1, new QTableWidgetItem(u.value("slug").toString()));
                                users_->setItem(i, 2, new QTableWidgetItem(role));
                                users_->setItem(i, 3, new QTableWidgetItem(QString::number(u.value("max_tunnels").toInt())));
                                users_->setItem(i, 4, new QTableWidgetItem(
                                    (u.value("online").toBool() ? QStringLiteral("在线 ") : QStringLiteral("离线 "))
                                    + (disabled ? QStringLiteral("已停用") : QString())));
                                auto *box = new QWidget;
                                auto *hb = new QHBoxLayout(box);
                                hb->setContentsMargins(0, 0, 0, 0);
                                hb->setSpacing(4);
                                auto *roleBtn = new QPushButton(role == "admin" ? QStringLiteral("降为用户")
                                                                                : QStringLiteral("设为管理员"));
                                roleBtn->setObjectName("link");
                                auto *quotaBtn = new QPushButton(QStringLiteral("配额"));
                                quotaBtn->setObjectName("link");
                                auto *disBtn = new QPushButton(disabled ? QStringLiteral("启用")
                                                                       : QStringLiteral("停用"));
                                disBtn->setObjectName(disabled ? "link" : "linkDanger");
                                hb->addWidget(roleBtn);
                                hb->addWidget(quotaBtn);
                                hb->addWidget(disBtn);
                                auto patch = [this, id](const QJsonObject &b) {
                                    st_->api()->request(st_->config().effectiveApiBase(),
                                                        st_->config().apiToken, "PATCH",
                                                        QStringLiteral("/api/admin/users/%1").arg(id), b,
                                                        [this](bool ok2, const QJsonObject &, const QString &e2) {
                                                            if (!ok2) {
                                                                QMessageBox::warning(this, QStringLiteral("失败"), e2);
                                                                return;
                                                            }
                                                            refresh();
                                                        });
                                };
                                connect(roleBtn, &QPushButton::clicked, this, [patch, role]() {
                                    QJsonObject b;
                                    b["role"] = role == "admin" ? "user" : "admin";
                                    patch(b);
                                });
                                connect(disBtn, &QPushButton::clicked, this, [patch, disabled]() {
                                    QJsonObject b;
                                    b["disabled"] = !disabled;
                                    patch(b);
                                });
                                connect(quotaBtn, &QPushButton::clicked, this, [this, patch]() {
                                    bool ok3 = false;
                                    const int v = QInputDialog::getInt(this, QStringLiteral("隧道配额"),
                                                                       QStringLiteral("最大隧道数"), 10, 0, 1000, 1, &ok3);
                                    if (!ok3) return;
                                    QJsonObject b;
                                    b["max_tunnels"] = v;
                                    patch(b);
                                });
                                users_->setCellWidget(i, 5, box);
                            }
                        });

    st_->api()->request(base, tok, "GET", "/api/admin/tunnels", {},
                        [this, base, tok](bool ok, const QJsonObject &o, const QString &) {
                            if (!ok) return;
                            const auto arr = o.value("tunnels").toArray();
                            tunnels_->setRowCount(arr.size());
                            for (int i = 0; i < arr.size(); ++i) {
                                const auto t = arr[i].toObject();
                                const int id = t.value("id").toInt();
                                const bool disabled = t.value("disabled").toBool();
                                tunnels_->setItem(i, 0, new QTableWidgetItem(QString::number(t.value("user_id").toInt())));
                                tunnels_->setItem(i, 1, new QTableWidgetItem(t.value("tunnel_id").toString()));
                                tunnels_->setItem(i, 2, new QTableWidgetItem(t.value("public_url").toString()));
                                auto *btn = new QPushButton(disabled ? QStringLiteral("启用")
                                                                     : QStringLiteral("停用"));
                                btn->setObjectName(disabled ? "link" : "linkDanger");
                                connect(btn, &QPushButton::clicked, this, [this, id, disabled]() {
                                    QJsonObject b;
                                    b["disabled"] = !disabled;
                                    st_->api()->request(st_->config().effectiveApiBase(),
                                                        st_->config().apiToken, "PATCH",
                                                        QStringLiteral("/api/admin/tunnels/%1").arg(id), b,
                                                        [this](bool ok2, const QJsonObject &, const QString &) {
                                                            if (ok2) refresh();
                                                        });
                                });
                                tunnels_->setCellWidget(i, 3, btn);
                            }
                        });

    st_->api()->request(base, tok, "GET", "/api/admin/audit?limit=80", {},
                        [this](bool ok, const QJsonObject &o, const QString &) {
                            if (!ok) return;
                            const auto arr = o.value("audit").toArray();
                            audit_->setRowCount(arr.size());
                            for (int i = 0; i < arr.size(); ++i) {
                                const auto a = arr[i].toObject();
                                audit_->setItem(i, 0, new QTableWidgetItem(a.value("at").toString().left(19)));
                                audit_->setItem(i, 1, new QTableWidgetItem(a.value("actor").toString()));
                                audit_->setItem(i, 2, new QTableWidgetItem(a.value("action").toString()));
                                audit_->setItem(i, 3, new QTableWidgetItem(a.value("detail").toString()));
                            }
                        });
}

} // namespace gui
