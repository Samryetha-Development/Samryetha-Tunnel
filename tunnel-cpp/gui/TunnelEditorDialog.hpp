#pragma once

#include "tunnel/Protocol.hpp"

#include <QDialog>

class QComboBox;
class QLineEdit;
class QPushButton;

namespace gui {

class TunnelEditorDialog : public QDialog {
    Q_OBJECT
public:
    explicit TunnelEditorDialog(const tunnel::TunnelDef &initial, bool isNew, QWidget *parent = nullptr);
    tunnel::TunnelDef result() const;

private:
    QLineEdit *id_;
    QComboBox *proto_;
    QLineEdit *sub_;
    QLineEdit *path_;
    QLineEdit *local_;
    QPushButton *okButton_ = nullptr;
    bool isNew_;
    void validate();
};

} // namespace gui
