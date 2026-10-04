#include "PageBase.h"

void PageBase::retranslate()
{
    for (const auto &p : reg_) {
        const QString text = I18n::instance().t(p.second);
        if (auto *l = qobject_cast<QLabel *>(p.first)) l->setText(text);
        else if (auto *b = qobject_cast<QAbstractButton *>(p.first)) b->setText(text);
        else if (auto *g = qobject_cast<QGroupBox *>(p.first)) g->setTitle(text);
    }
    for (auto it = combos_.begin(); it != combos_.end(); ++it)
        for (int i = 0; i < it.value().size(); ++i)
            it.key()->setItemText(i, I18n::instance().t(it.value().at(i)));
    onRetranslate();
}

void PageBase::header(QVBoxLayout *page, const char *titleKey, const char *subKey)
{
    auto *t = R(new QLabel, titleKey);
    t->setObjectName("h1");
    auto *s = R(new QLabel, subKey);
    s->setObjectName("sub");
    s->setWordWrap(true);
    page->addWidget(t);
    page->addWidget(s);
    page->addSpacing(6);
}

QVBoxLayout *PageBase::card(QVBoxLayout *page, const char *titleKey)
{
    auto *f = new QFrame;
    f->setObjectName("card");
    auto *v = new QVBoxLayout(f);
    v->setContentsMargins(18, 16, 18, 18);
    v->setSpacing(10);
    auto *t = R(new QLabel, titleKey);
    t->setObjectName("cardTitle");
    v->addWidget(t);
    page->addWidget(f);
    return v;
}

void PageBase::fillCombo(QComboBox *c, const QList<QPair<QString, QString>> &items)
{
    c->clear();
    QStringList keys;
    for (const auto &it : items) {
        c->addItem(I18n::instance().t(it.second), it.first);
        keys << it.second;
    }
    combos_.insert(c, keys);
}
