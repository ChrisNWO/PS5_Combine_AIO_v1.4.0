#pragma once
#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QPushButton>

#include "PageBase.h"
#include "PathRow.h"

// «Бэкпорт»: понижение версии SDK в исполняемых файлах игры (ELF/SELF) и их fake-подпись — бэкенд Auto-Backpork (Nazky),
// собранный как tools\backpork\backpork-cli.exe. Работает с папкой игры, полученной с ВАШЕЙ консоли (дамп).
// Системные библиотеки fakelib — файлы Sony, программа их не содержит: вы указываете свою папку с ними.
class BackportPage : public PageBase
{
    Q_OBJECT
public:
    explicit BackportPage(QWidget *parent = nullptr);
    void setPath(const QString &path) override;
protected:
    void onRetranslate() override;
private:
    void start();
    void suggestOutput();
    void updateInfo();
    void updateModeUi();

    QComboBox *mode_, *target_;
    PathRow *src_, *dst_, *fakelib_;
    QCheckBox *noLibc_, *noBackup_, *overwrite_;
    QLabel *info_;
    QPushButton *run_;
};
