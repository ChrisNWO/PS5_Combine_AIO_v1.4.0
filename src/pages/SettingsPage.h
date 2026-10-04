#pragma once
#include <QCheckBox>
#include <QComboBox>
#include <QGridLayout>
#include <QLineEdit>
#include <QPushButton>

#include "PageBase.h"

class SettingsPage : public PageBase
{
    Q_OBJECT
public:
    explicit SettingsPage(QWidget *parent = nullptr);
protected:
    void onRetranslate() override { refreshTools(); }
private:
    void refreshTools();

    QComboBox *lang_, *theme_;
    QLineEdit *consoleFw_, *maxFw_;
    QCheckBox *amprAuto_, *amprForce_;
    QComboBox *amprVer_;
    QCheckBox *ftpEnable_, *ftpAnon_;
    QLineEdit *ftpHost_, *ftpPort_, *ftpPath_, *ftpUser_, *ftpPass_;
    QGridLayout *grid_;
    QList<QWidget *> rows_;
};
