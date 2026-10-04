#pragma once
#include <QCheckBox>
#include <QComboBox>
#include <QLineEdit>
#include <QPushButton>

#include "PageBase.h"
#include "PathRow.h"

// Инспекция / список / проверка / распаковка: .pkg -> fpkg-cli, образы PFS/exFAT -> mkpfs.
class ToolsPage : public PageBase
{
    Q_OBJECT
public:
    explicit ToolsPage(QWidget *parent = nullptr);
    void setPath(const QString &path) override { file_->setText(path); }
protected:
    void onRetranslate() override;
    void applyBusy() override;
private:
    enum Action { Info, List, Verify, Extract };
    enum Maint { Repair, Ampr, Edit, Convert, VerifyImg, Rebuild };
    void run(Action a);
    void runMaint(Maint m);
    bool isPkg() const;

    PathRow *file_, *out_;
    QLineEdit *passcode_;
    QCheckBox *full_, *overwrite_;
    QPushButton *bInfo_, *bList_, *bVerify_, *bExtract_;
    QPushButton *bRepair_, *bAmpr_, *bEdit_, *bConvert_, *bVerifyImg_, *bRebuild_;
    QComboBox *convTarget_;
    PathRow *convOut_;
    QLabel *kind_;
};
