#pragma once
#include <QCheckBox>
#include <QComboBox>
#include <QPushButton>
#include <QSpinBox>

#include <QShowEvent>

#include "PageBase.h"
#include "PathRow.h"

// Упаковка в PFS-образы (.ffpfsc / .ffpfs / .exfat) — бэкенд mkpfs (PSBrew).
class PackPage : public PageBase
{
    Q_OBJECT
public:
    explicit PackPage(QWidget *parent = nullptr);
    void setPath(const QString &path) override;
protected:
    void onRetranslate() override;
    void showEvent(QShowEvent *e) override;
    void applyBusy() override { run_->setEnabled(!busy_); }
private:
    void onModeChanged();
    void suggestOutput();
    void start();

    QComboBox *mode_, *version_, *cluster_;
    QSpinBox *level_;
    PathRow *src_, *dst_;
    QCheckBox *noCompress_, *caseSens_, *verify_, *verbose_, *ftpUpload_;
    QPushButton *run_;
    QLabel *hint_;
};
