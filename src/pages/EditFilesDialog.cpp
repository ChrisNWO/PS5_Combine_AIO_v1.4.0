#include "EditFilesDialog.h"

#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

#include "I18n.h"

// kind: replace | add | add-tree | mkdir | delete
EditFilesDialog::EditFilesDialog(QWidget *parent) : QDialog(parent)
{
    setWindowTitle(TR("edit.title"));
    resize(820, 460);
    auto *v = new QVBoxLayout(this);
    auto *hint = new QLabel(TR("edit.hint"));
    hint->setWordWrap(true);
    hint->setObjectName("muted");
    v->addWidget(hint);

    table_ = new QTableWidget(0, 3);
    table_->setHorizontalHeaderLabels({TR("edit.col.action"), TR("edit.col.imagePath"), TR("edit.col.source")});
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setShowGrid(false);
    table_->verticalHeader()->hide();
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->setColumnWidth(0, 170);
    table_->setColumnWidth(1, 300);
    v->addWidget(table_, 1);

    auto *bar = new QHBoxLayout;
    struct B { const char *key; const char *kind; };
    for (const B &b : {B{"edit.replace", "replace"}, B{"edit.add", "add"}, B{"edit.addTree", "add-tree"},
                       B{"edit.mkdir", "mkdir"}, B{"edit.delete", "delete"}}) {
        auto *btn = new QPushButton(I18n::instance().t(QString::fromLatin1(b.key)));
        const QString kind = QString::fromLatin1(b.kind);
        connect(btn, &QPushButton::clicked, this, [this, kind] { addOperation(kind); });
        bar->addWidget(btn);
    }
    auto *rm = new QPushButton(TR("edit.removeRow"));
    connect(rm, &QPushButton::clicked, this, [this] {
        const auto rows = table_->selectionModel()->selectedRows();
        if (!rows.isEmpty()) table_->removeRow(rows.first().row());
    });
    bar->addStretch(1);
    bar->addWidget(rm);
    v->addLayout(bar);

    auto *box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    box->button(QDialogButtonBox::Ok)->setText(TR("edit.apply"));
    box->button(QDialogButtonBox::Cancel)->setText(TR("common.cancel"));
    connect(box, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);
    v->addWidget(box);
}

void EditFilesDialog::addOperation(const QString &kind)
{
    bool ok = false;
    const QString imagePath = QInputDialog::getText(this, TR("edit.title"), TR("edit.askImagePath"), QLineEdit::Normal,
                                                    kind == QLatin1String("replace") ? QStringLiteral("sce_sys/param.json") : QString(), &ok).trimmed();
    if (!ok || imagePath.isEmpty()) return;

    QString source;
    if (kind == QLatin1String("replace") || kind == QLatin1String("add")) {
        source = QFileDialog::getOpenFileName(this, TR("edit.pickFile"));
        if (source.isEmpty()) return;
    } else if (kind == QLatin1String("add-tree")) {
        source = QFileDialog::getExistingDirectory(this, TR("edit.pickFolder"));
        if (source.isEmpty()) return;
    }
    const int r = table_->rowCount();
    table_->insertRow(r);
    auto *k = new QTableWidgetItem(I18n::instance().t(QStringLiteral("edit.kind.") + kind));
    k->setData(Qt::UserRole, kind);
    table_->setItem(r, 0, k);
    table_->setItem(r, 1, new QTableWidgetItem(imagePath));
    table_->setItem(r, 2, new QTableWidgetItem(QDir::toNativeSeparators(source)));
}

QStringList EditFilesDialog::helperArgs() const
{
    QStringList a;
    for (int r = 0; r < table_->rowCount(); ++r) {
        const QString kind = table_->item(r, 0)->data(Qt::UserRole).toString();
        const QString img = table_->item(r, 1)->text();
        const QString src = table_->item(r, 2)->text();
        a << QStringLiteral("--") + kind << img;
        if (!src.isEmpty()) a << src;
    }
    return a;
}
