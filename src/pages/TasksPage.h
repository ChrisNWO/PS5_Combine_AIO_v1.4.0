#pragma once
#include <QCheckBox>
#include <QPushButton>
#include <QTableWidget>

#include "PageBase.h"

// «Задачи»: очередь запусков с сохранением между перезапусками.
class TasksPage : public PageBase
{
    Q_OBJECT
public:
    explicit TasksPage(JobPanel *jobs, QWidget *parent = nullptr);
protected:
    void onRetranslate() override;
private:
    void refresh();
    qint64 selectedId() const;

    JobPanel *jobs_;
    QTableWidget *table_;
    QPushButton *bStart_, *bRetry_, *bRemove_, *bClear_;
    QCheckBox *auto_;
};
