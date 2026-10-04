#pragma once
#include <QDialog>
#include <QList>
#include <QTableWidget>
#include <QTreeWidget>
#include <QComboBox>
#include <QLabel>

#include "LibraryScanner.h"

// Дубликаты: элементы с одинаковым Content ID и версией (одна и та же сборка в разных местах/форматах).
// Ничего не удаляется само: пользователь отмечает лишние копии, и они уходят в Корзину после подтверждения.
class DuplicatesDialog : public QDialog
{
    Q_OBJECT
public:
    DuplicatesDialog(const QList<LibraryItem> &items, QWidget *parent = nullptr);
    bool changed() const { return changed_; }    // что-то отправили в Корзину → нужен пересканирование
private:
    void markExtras();
    void trashChecked();
    void openSelected();
    QTreeWidget *tree_;
    QLabel *info_;
    bool changed_ = false;
};

// Переименование по шаблону: {TITLE} {TITLE_ID} {CONTENT_ID} {VERSION} {CATEGORY} {REGION} {PLATFORM} {FW} {SDK}.
// Показывает «было → станет» и применяет только отмеченное; существующие имена не перезаписываются.
class RenameDialog : public QDialog
{
    Q_OBJECT
public:
    RenameDialog(const QList<LibraryItem> &items, QWidget *parent = nullptr);
    bool changed() const { return changed_; }
private:
    void updatePreview();
    void apply();
    QString newBaseName(const LibraryItem &it) const;
    QComboBox *format_;
    QTableWidget *table_;
    QLabel *info_;
    QList<LibraryItem> items_;
    bool changed_ = false;
};
