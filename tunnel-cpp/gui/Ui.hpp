#pragma once

#include <QFrame>
#include <QLabel>
#include <QString>
#include <QVBoxLayout>

namespace ui {

inline QLabel *eyebrow(const QString &text) {
    auto *l = new QLabel(text.toUpper());
    l->setObjectName("eyebrow");
    return l;
}

inline QLabel *pageTitle(const QString &text) {
    auto *l = new QLabel(text);
    l->setObjectName("pageTitle");
    return l;
}

inline QLabel *sectionTitle(const QString &text) {
    auto *l = new QLabel(text);
    l->setObjectName("sectionTitle");
    return l;
}

/// 状态：小圆点 + 文字（对齐你站上 “● 0 online”）
inline QLabel *pill(const QString &text, bool ok) {
    auto *l = new QLabel(QStringLiteral("<span style='color:%1'>●</span>&nbsp;%2")
                             .arg(ok ? "#3ddc84" : "#5c5c5c", text));
    l->setTextFormat(Qt::RichText);
    l->setObjectName("muted");
    return l;
}

inline QLabel *chip(const QString &text) {
    auto *l = new QLabel(text.toLower());
    l->setObjectName("mono");
    l->setStyleSheet(QStringLiteral("color:#6f6f6f;font-size:11px;"));
    return l;
}

/// 指标：大数字在上、小标签在下，无边框无底色
inline QWidget *metric(const QString &label, QLabel **valueOut) {
    auto *w = new QWidget;
    auto *v = new QVBoxLayout(w);
    v->setContentsMargins(0, 4, 0, 4);
    v->setSpacing(4);
    auto *val = new QLabel("—");
    val->setObjectName("metricValue");
    auto *lab = new QLabel(label);
    lab->setObjectName("metricLabel");
    v->addWidget(val);
    v->addWidget(lab);
    *valueOut = val;
    return w;
}

/// 兼容旧调用
inline QFrame *statTile(const QString &label, const QString &, QLabel **valueOut) {
    auto *f = new QFrame;
    auto *v = new QVBoxLayout(f);
    v->setContentsMargins(0, 4, 0, 4);
    v->setSpacing(4);
    auto *val = new QLabel("—");
    val->setObjectName("metricValue");
    auto *lab = new QLabel(label);
    lab->setObjectName("metricLabel");
    v->addWidget(val);
    v->addWidget(lab);
    *valueOut = val;
    return f;
}

inline QFrame *card() {
    auto *f = new QFrame;
    f->setStyleSheet(QStringLiteral("background:transparent;border:0;"));
    return f;
}

inline QFrame *separator() {
    auto *f = new QFrame;
    f->setObjectName("sep");
    f->setFixedHeight(1);
    return f;
}

} // namespace ui
