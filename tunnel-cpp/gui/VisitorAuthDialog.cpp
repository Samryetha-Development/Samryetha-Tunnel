#include "VisitorAuthDialog.hpp"

#include <QDialogButtonBox>
#include <QJsonArray>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QVBoxLayout>

namespace gui {

VisitorAuthDialog::VisitorAuthDialog(const QString &tunnelId, const QJsonObject &current,
                                     QWidget *parent)
    : QDialog(parent), tunnelId_(tunnelId) {
    setWindowTitle(QStringLiteral("访客鉴权 · %1").arg(tunnelId));
    setMinimumWidth(430);

    user_ = new QLineEdit;
    pass_ = new QLineEdit;
    pass_->setEchoMode(QLineEdit::Password);
    ips_ = new QLineEdit;
    ips_->setPlaceholderText(QStringLiteral("1.2.3.4, 10.0.0.0/8（留空=不限制）"));

    if (current.contains("basic")) {
        const auto b = current.value("basic").toObject();
        user_->setText(b.value("user").toString());
        pass_->setText(b.value("pass").toString());
        hasBasic_ = true;
    }
    if (current.contains("ips")) {
        QStringList list;
        for (const auto &v : current.value("ips").toArray())
            list << v.toString();
        ips_->setText(list.join(QStringLiteral(", ")));
        hasIps_ = true;
    }

    auto *form = new QFormLayout;
    form->addRow(QStringLiteral("Basic 用户名"), user_);
    form->addRow(QStringLiteral("Basic 密码"), pass_);
    form->addRow(QStringLiteral("IP 白名单"), ips_);

    auto *hint = new QLabel(QStringLiteral("留空用户名则不启用 Basic；重连客户端后生效。"));
    hint->setObjectName("muted");

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *root = new QVBoxLayout(this);
    root->addLayout(form);
    root->addWidget(hint);
    root->addWidget(buttons);
}

QJsonObject VisitorAuthDialog::result() const {
    QJsonObject va;
    if (!user_->text().trimmed().isEmpty()) {
        QJsonObject b;
        b["user"] = user_->text().trimmed();
        b["pass"] = pass_->text();
        va["basic"] = b;
    }
    const QString ips = ips_->text().trimmed();
    if (!ips.isEmpty()) {
        QJsonArray arr;
        for (const QString &p : ips.split(',', Qt::SkipEmptyParts))
            arr.append(p.trimmed());
        va["ips"] = arr;
    }
    return va;
}

} // namespace gui
