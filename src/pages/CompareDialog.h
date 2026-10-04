#pragma once
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QJsonObject>
#include <QLabel>
#include <QList>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QTabWidget>
#include <QTableWidget>

#include "LibraryScanner.h"

class ToolRunner;

// Сверка двух образов / дампов / пакетов: набор файлов, размеры, (по желанию) побайтово, поля param.json, корень игры.
// Типичный сценарий: слева наш образ, справа заведомо рабочий — по отчёту видно, чего не хватает и что отличается.
class CompareDialog : public QDialog
{
    Q_OBJECT
public:
    CompareDialog(const QList<LibraryItem> &items, int preselectA, QWidget *parent = nullptr);
    ~CompareDialog() override;

private:
    void start();
    void cancel();
    void onFinished(bool ok, const QString &err);
    void showReport(const QJsonObject &r);
    void saveReport();
    QString label(const LibraryItem &it) const;
    QString fmtBytes(qint64 b) const;

    QList<LibraryItem> items_;
    QComboBox *a_, *b_;
    QCheckBox *deep_;
    QPushButton *bStart_, *bCancel_, *bSave_;
    QProgressBar *bar_;
    QLabel *status_;
    QTabWidget *tabs_;
    QPlainTextEdit *summary_;
    QTableWidget *onlyA_, *onlyB_, *differ_, *params_, *geometry_;
    ToolRunner *runner_ = nullptr;
    QString workDir_, reportPath_, reportText_;
};
