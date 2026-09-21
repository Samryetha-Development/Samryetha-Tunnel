#include "TunnelEditorDialog.hpp"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QVBoxLayout>

using namespace tunnel;

namespace gui {

TunnelEditorDialog::TunnelEditorDialog(const TunnelDef &initial, bool isNew, QWidget *parent)
    : QDialog(parent), isNew_(isNew) {
    setWindowTitle(isNew ? QStringLiteral("新隧道") : QStringLiteral("编辑隧道"));
    setMinimumWidth(420);

    id_ = new QLineEdit(initial.tunnelId);
    id_->setPlaceholderText(QStringLiteral("如 web1（唯一）"));
    proto_ = new QComboBox;
    proto_->addItems({QStringLiteral("http"), QStringLiteral("tcp")});
    proto_->setCurrentText(initial.proto.isEmpty() ? QStringLiteral("http") : initial.proto);
    sub_ = new QLineEdit(initial.subdomain);
    sub_->setPlaceholderText(QStringLiteral("如 app1（留空则不用）"));
    path_ = new QLineEdit(initial.pathPrefix);
    path_->setPlaceholderText(QStringLiteral("如 /api（留空则自动 /用户/隧道）"));
    local_ = new QLineEdit(initial.localAddr);
    local_->setPlaceholderText(QStringLiteral("如 127.0.0.1:8080"));

    auto *form = new QFormLayout;
    form->addRow(QStringLiteral("隧道 ID"), id_);
    form->addRow(QStringLiteral("类型"), proto_);
    form->addRow(QStringLiteral("子域名"), sub_);
    form->addRow(QStringLiteral("子路由"), path_);
    form->addRow(QStringLiteral("本地地址"), local_);

    auto *hint = new QLabel(QStringLiteral("保存后需重连生效。子域名/子路由会自动带用户名命名空间。"));
    hint->setObjectName("muted");

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, [this]() { validate(); });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    okButton_ = buttons->button(QDialogButtonBox::Ok);
    okButton_->setText(isNew ? QStringLiteral("添加") : QStringLiteral("保存"));

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(hint);
    layout->addWidget(buttons);
    connect(id_, &QLineEdit::textChanged, this, &TunnelEditorDialog::validate);
    connect(local_, &QLineEdit::textChanged, this, &TunnelEditorDialog::validate);
    validate();
}

TunnelDef TunnelEditorDialog::result() const {
    TunnelDef d;
    d.tunnelId = id_->text().trimmed();
    d.proto = proto_->currentText();
    d.subdomain = sub_->text().trimmed().toLower();
    d.pathPrefix = path_->text().trimmed();
    d.localAddr = local_->text().trimmed();
    return d;
}

void TunnelEditorDialog::validate() {
    bool ok = !id_->text().trimmed().isEmpty();
    const QRegularExpression addr(QStringLiteral("^[^:]+:[0-9]{1,5}$"));
    if (!addr.match(local_->text().trimmed()).hasMatch())
        ok = false;
    if (okButton_)
        okButton_->setEnabled(ok);
}

} // namespace gui
