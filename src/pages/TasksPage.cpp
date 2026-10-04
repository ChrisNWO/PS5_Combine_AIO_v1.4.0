#include "TasksPage.h"

#include <QDateTime>
#include <QHBoxLayout>
#include <QHeaderView>

#include "I18n.h"
#include "JobPanel.h"

TasksPage::TasksPage(JobPanel *jobs, QWidget *parent) : PageBase(parent), jobs_(jobs)
{
    auto *page = new QVBoxLayout(this);
    page->setContentsMargins(28, 24, 28, 12);
    page->setSpacing(12);
    header(page, "tasks.title", "tasks.subtitle");

    auto *bar = new QHBoxLayout;
    bStart_ = R(new QPushButton, "tasks.start");
    bStart_->setObjectName("primary");
    bRetry_ = R(new QPushButton, "tasks.retry");
    bRemove_ = R(new QPushButton, "tasks.remove");
    bClear_ = R(new QPushButton, "tasks.clearDone");
    auto_ = R(new QCheckBox, "tasks.autostart");
    auto_->setChecked(jobs_->autoStart());
    bar->addWidget(bStart_);
    bar->addWidget(bRetry_);
    bar->addWidget(bRemove_);
    bar->addWidget(bClear_);
    bar->addStretch(1);
    bar->addWidget(auto_);
    page->addLayout(bar);

    table_ = new QTableWidget(0, 4);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setShowGrid(false);
    table_->verticalHeader()->hide();
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->setMinimumHeight(260);
    page->addWidget(table_, 1);

    connect(jobs_, &JobPanel::tasksChanged, this, &TasksPage::refresh);
    connect(bStart_, &QPushButton::clicked, this, [this] { jobs_->startQueue(); });
    connect(bRetry_, &QPushButton::clicked, this, [this] { if (selectedId()) jobs_->retry(selectedId()); });
    connect(bRemove_, &QPushButton::clicked, this, [this] { if (selectedId()) jobs_->removeTask(selectedId()); });
    connect(bClear_, &QPushButton::clicked, this, [this] { jobs_->clearFinished(); });
    connect(auto_, &QCheckBox::toggled, this, [this](bool on) { jobs_->setAutoStart(on); });
    onRetranslate();
}

qint64 TasksPage::selectedId() const
{
    const auto rows = table_->selectionModel()->selectedRows();
    if (rows.isEmpty()) return 0;
    return table_->item(rows.first().row(), 0)->data(Qt::UserRole).toLongLong();
}

void TasksPage::onRetranslate()
{
    table_->setHorizontalHeaderLabels({TR("tasks.col.task"), TR("tasks.col.state"), TR("tasks.col.finished"), TR("tasks.col.result")});
    refresh();
}

void TasksPage::refresh()
{
    static const char *states[] = {"tasks.state.queued", "tasks.state.running", "tasks.state.done",
                                   "tasks.state.failed", "tasks.state.cancelled", "tasks.state.interrupted"};
    const qint64 keep = selectedId();
    table_->setRowCount(0);
    const auto &list = jobs_->tasks();
    for (int i = list.size() - 1; i >= 0; --i) {            // новые сверху
        const TaskInfo &t = list.at(i);
        const int r = table_->rowCount();
        table_->insertRow(r);
        auto *title = new QTableWidgetItem(t.title);
        title->setData(Qt::UserRole, t.id);
        title->setToolTip(t.title);
        auto *state = new QTableWidgetItem(I18n::instance().t(QString::fromLatin1(states[t.state])));
        if (t.state == TaskInfo::Failed) state->setForeground(QColor(QStringLiteral("#ff6b6b")));
        else if (t.state == TaskInfo::Done) state->setForeground(QColor(QStringLiteral("#3ecf8e")));
        else if (t.state == TaskInfo::Interrupted || t.state == TaskInfo::Cancelled) state->setForeground(QColor(QStringLiteral("#ff9f43")));
        auto *when = new QTableWidgetItem(t.finishedAt ? QDateTime::fromSecsSinceEpoch(t.finishedAt).toString(QStringLiteral("dd.MM.yyyy HH:mm:ss")) : QString());
        auto *res = new QTableWidgetItem(t.error.left(300).replace(QLatin1Char('\n'), QLatin1Char(' ')));
        res->setToolTip(t.error);
        table_->setItem(r, 0, title);
        table_->setItem(r, 1, state);
        table_->setItem(r, 2, when);
        table_->setItem(r, 3, res);
        if (t.id == keep) table_->selectRow(r);
    }
    table_->setColumnWidth(0, 420);
    table_->setColumnWidth(1, 150);
    table_->setColumnWidth(2, 170);
}
