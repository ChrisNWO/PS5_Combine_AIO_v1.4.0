#pragma once
#include <QCheckBox>
#include <QLabel>
#include <QComboBox>
#include <QFormLayout>
#include <QShowEvent>
#include <QLineEdit>
#include <QRegularExpression>
#include <QPushButton>

#include "PageBase.h"
#include "PathRow.h"

// Сборка PS5 FPKG (.pkg) — бэкенд fpkg-cli (PSVIETHOA) + Sony Publishing Tools.
// Источник: папка игры ИЛИ образ (.exfat / .ffpkg / .ffpfsc) / проект .gp5 — образы fpkg-cli распаковывает сам.
class FpkgPage : public PageBase
{
    Q_OBJECT
public:
    explicit FpkgPage(QWidget *parent = nullptr);
    void setPath(const QString &path) override;
protected:
    void onRetranslate() override;
    void showEvent(QShowEvent *e) override;
    void applyBusy() override { run_->setEnabled(!busy_); }
private:
    void onSourceModeChanged();
    void refreshInfo();
    void start();
    void startSdk279(const QString &source, const QString &output);
    static void setState(QLabel *l, const char *objectName);

    QFormLayout *srcForm_, *optForm_;
    QComboBox *srcMode_, *exfatMode_;
    QLabel *srcLabel_;
    PathRow *src_, *out_, *temp_;
    QLabel *iTitle_, *iTitleId_, *iContent_, *iVersion_, *iFw_, *iSdk_, *warn_, *fwNote_, *imageNote_;
    QComboBox *kind_, *preset_, *sdk_;
    QLineEdit *contentId_, *title_, *version_;
    QCheckBox *keepFw_, *fullVerify_, *sha_, *overwrite_, *clean_, *noSdk_, *ftpUpload_;
    QPushButton *run_;

    // Необязательный альтернативный движок (tools/fpkg279): AC-пакеты, патч + .remastered.pkg, PlayGo.
    QComboBox *engine_, *volume_;
    QLineEdit *entitlementKey_, *playgoLangs_;
    PathRow *reference_;
    QCheckBox *keepIntermediate279_;
    QLabel *sizeEstimate_;
    void onEngineChanged();
    void updateSizeEstimate();
    QVBoxLayout *card279_ = nullptr;   // card() возвращает layout внутри QFrame; сам QFrame — его parentWidget()
};
