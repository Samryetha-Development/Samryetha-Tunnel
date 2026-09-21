#pragma once

#include <QFrame>
#include <QLabel>
#include <QString>
#include <QVBoxLayout>

namespace ui {

inline QLabel *sectionTitle(const QString &text) {
    auto *l = new QLabel(text.toUpper());
    l->setStyleSheet(QStringLiteral("color:#7d8ba1;font-size:11px;font-weight:700;letter-spacing:.6px;"));
    return l;
}

inline QLabel *pill(const QString &text, bool ok) {
    auto *l = new QLabel(text);
    l->setObjectName(ok ? "pillOk" : "pillOff");
    return l;
}

inline QLabel *chip(const QString &text) {
    auto *l = new QLabel(text);
    l->setObjectName("chip");
    return l;
}

/// 指标卡：返回外框，valueOut 用于更新数值
inline QFrame *statTile(const QString &label, const QString &accent, QLabel **valueOut) {
    auto *f = new QFrame;
    f->setObjectName("tile");
    f->setMinimumHeight(84);
    auto *v = new QVBoxLayout(f);
    v->setContentsMargins(15, 13, 15, 13);
    v->setSpacing(5);
    auto *top = new QLabel(QStringLiteral("<span style='color:%1'>●</span>&nbsp;%2").arg(accent, label));
    top->setObjectName("cardLabel");
    top->setTextFormat(Qt::RichText);
    auto *val = new QLabel("—");
    val->setObjectName("cardValue");
    v->addWidget(top);
    v->addWidget(val);
    *valueOut = val;
    return f;
}

inline QFrame *card() {
    auto *f = new QFrame;
    f->setObjectName("card");
    return f;
}

} // namespace ui
