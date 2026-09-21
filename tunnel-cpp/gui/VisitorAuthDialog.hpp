#pragma once

#include <QDialog>
#include <QJsonObject>

class QLineEdit;

namespace gui {

/// 编辑某条隧道的访客侧鉴权（BasicAuth + IP 白名单）
class VisitorAuthDialog : public QDialog {
    Q_OBJECT
public:
    VisitorAuthDialog(const QString &tunnelId, const QJsonObject &current, QWidget *parent = nullptr);
    QJsonObject result() const;

private:
    QString tunnelId_;
    QLineEdit *user_;
    QLineEdit *pass_;
    QLineEdit *ips_;
    bool hasBasic_ = false;
    bool hasIps_ = false;
};

} // namespace gui
