#pragma once
#include <QDialog>
#include <QStringList>
#include <QTableWidget>

// Список правок для exFAT-образа: заменить / добавить файл / добавить папку / создать каталог / удалить.
// Правки применяются транзакционно (на подготовленной копии с проверкой, затем атомарная подмена) — так делает PS5 PKG Tool.
class EditFilesDialog : public QDialog
{
    Q_OBJECT
public:
    explicit EditFilesDialog(QWidget *parent = nullptr);
    QStringList helperArgs() const;     // аргументы для «ps5aio-helper edit <image> …» (без пути к образу)
    int operationCount() const { return table_->rowCount(); }

private:
    void addOperation(const QString &kind);
    QTableWidget *table_;
};
