#pragma once
#include <QAbstractButton>
#include <QComboBox>
#include <QFrame>
#include <QGroupBox>
#include <QLabel>
#include <QList>
#include <QMap>
#include <QPair>
#include <QVBoxLayout>
#include <QWidget>

#include "I18n.h"
#include "JobPanel.h"

// База страниц: регистрирует подписи по ключам и умеет перевести их на лету.
class PageBase : public QWidget
{
    Q_OBJECT
public:
    explicit PageBase(QWidget *parent = nullptr) : QWidget(parent) {}
    void retranslate();
    void setBusy(bool b) { busy_ = b; applyBusy(); }
    virtual void setPath(const QString &) {}      // «передать в…» из Библиотеки

signals:
    void runRequested(const QList<JobSpec> &jobs, const QString &openPath);

protected:
    virtual void onRetranslate() {}
    virtual void applyBusy() {}

    template <class W> W *R(W *w, const char *key)
    {
        reg_.append({w, QString::fromLatin1(key)});
        return w;
    }
    // Заголовок страницы + подзаголовок.
    void header(QVBoxLayout *page, const char *titleKey, const char *subKey);
    // Карточка с заголовком; возвращает layout внутри неё.
    QVBoxLayout *card(QVBoxLayout *page, const char *titleKey);
    // Выпадающий список из пар (код, ключ перевода).
    void fillCombo(QComboBox *c, const QList<QPair<QString, QString>> &items);
    static QString code(const QComboBox *c) { return c->currentData().toString(); }

    bool busy_ = false;

private:
    QList<QPair<QWidget *, QString>> reg_;
    QMap<QComboBox *, QStringList> combos_;
};
