#include "LibraryDialogs.h"

#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHash>
#include <QHeaderView>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QSet>
#include <QUrl>
#include <QVBoxLayout>

#include "I18n.h"

static QString fmtBytes(qint64 b)
{
    auto &i = I18n::instance();
    const double v = double(b);
    if (b >= (1LL << 30)) return QString::number(v / double(1LL << 30), 'f', 1) + QLatin1Char(' ') + i.t("unit.gb");
    if (b >= (1LL << 20)) return QString::number(v / double(1LL << 20), 'f', 1) + QLatin1Char(' ') + i.t("unit.mb");
    if (b >= (1LL << 10)) return QString::number(v / double(1LL << 10), 'f', 0) + QLatin1Char(' ') + i.t("unit.kb");
    return QString::number(b) + QLatin1Char(' ') + i.t("unit.b");
}

// ================================================================= дубликаты

enum { DupPathRole = Qt::UserRole + 1 };

DuplicatesDialog::DuplicatesDialog(const QList<LibraryItem> &items, QWidget *parent) : QDialog(parent)
{
    setWindowTitle(TR("dup.title"));
    resize(1000, 560);
    auto *v = new QVBoxLayout(this);
    info_ = new QLabel;
    info_->setWordWrap(true);
    info_->setObjectName("muted");
    v->addWidget(info_);

    tree_ = new QTreeWidget;
    tree_->setColumnCount(5);
    tree_->setHeaderLabels({TR("dup.col.item"), TR("lib.col.source"), TR("lib.col.size"), TR("lib.col.fw"), TR("lib.col.path")});
    tree_->setRootIsDecorated(true);
    tree_->setSelectionMode(QAbstractItemView::SingleSelection);
    v->addWidget(tree_, 1);

    // Ключ как у PS5 PKG Tool (Content ID), плюс версия: один и тот же Content ID с другой версией — не дубликат.
    QHash<QString, QList<int>> groups;
    QStringList order;
    for (int i = 0; i < items.size(); ++i) {
        const LibraryItem &it = items.at(i);
        QString key;
        if (!it.contentId.isEmpty()) key = QStringLiteral("c:") + it.contentId + QLatin1Char('|') + it.version;
        else if (!it.titleId.isEmpty()) key = QStringLiteral("t:") + it.titleId + QLatin1Char('|') + it.version + QLatin1Char('|') + QString::number(it.size);
        else key = QStringLiteral("f:") + it.fileName.toLower() + QLatin1Char('|') + QString::number(it.size);
        if (!groups.contains(key)) order << key;
        groups[key].append(i);
    }
    int sets = 0, extras = 0;
    qint64 extraBytes = 0;
    for (const QString &key : std::as_const(order)) {
        const QList<int> &m = groups[key];
        if (m.size() < 2) continue;
        ++sets;
        const LibraryItem &first = items.at(m.first());
        auto *g = new QTreeWidgetItem(tree_);
        g->setText(0, QStringLiteral("%1 [%2] %3 · ×%4").arg(first.title, first.titleId, first.version).arg(m.size()));
        QFont f = g->font(0);
        f.setBold(true);
        g->setFont(0, f);
        g->setFlags(Qt::ItemIsEnabled);
        for (int k = 0; k < m.size(); ++k) {
            const LibraryItem &it = items.at(m.at(k));
            auto *c = new QTreeWidgetItem(g);
            c->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable);
            c->setCheckState(0, Qt::Unchecked);
            c->setText(0, it.fileName);
            c->setText(1, it.sourceDetail.isEmpty() ? it.source.toUpper() : it.sourceDetail);
            c->setText(2, fmtBytes(it.size));
            c->setText(3, it.requiredFw);
            c->setText(4, it.path);
            c->setData(0, DupPathRole, it.path);
            c->setData(0, Qt::UserRole, it.isDump);
            c->setData(2, Qt::UserRole, it.size);
            if (k > 0) { ++extras; extraBytes += it.size; }
        }
        g->setExpanded(true);
    }
    info_->setText(sets == 0 ? TR("dup.none") : I18n::instance().t("dup.summary", {sets, extras, fmtBytes(extraBytes)}));
    tree_->resizeColumnToContents(0);
    tree_->setColumnWidth(0, qMax(tree_->columnWidth(0), 320));
    tree_->resizeColumnToContents(1);
    tree_->resizeColumnToContents(2);

    auto *bar = new QHBoxLayout;
    auto *bMark = new QPushButton(TR("dup.mark"));
    auto *bOpen = new QPushButton(TR("dup.open"));
    auto *bTrash = new QPushButton(TR("dup.trash"));
    auto *bClose = new QPushButton(TR("dup.close"));
    bMark->setEnabled(sets > 0);
    bTrash->setEnabled(sets > 0);
    bar->addWidget(bMark);
    bar->addWidget(bOpen);
    bar->addStretch(1);
    bar->addWidget(bTrash);
    bar->addWidget(bClose);
    v->addLayout(bar);

    connect(bMark, &QPushButton::clicked, this, &DuplicatesDialog::markExtras);
    connect(bOpen, &QPushButton::clicked, this, &DuplicatesDialog::openSelected);
    connect(bTrash, &QPushButton::clicked, this, &DuplicatesDialog::trashChecked);
    connect(bClose, &QPushButton::clicked, this, &QDialog::accept);
}

// Оставляем первую копию в каждой группе, остальные отмечаем.
void DuplicatesDialog::markExtras()
{
    for (int g = 0; g < tree_->topLevelItemCount(); ++g) {
        QTreeWidgetItem *grp = tree_->topLevelItem(g);
        for (int k = 0; k < grp->childCount(); ++k)
            grp->child(k)->setCheckState(0, k == 0 ? Qt::Unchecked : Qt::Checked);
    }
}

void DuplicatesDialog::openSelected()
{
    const auto sel = tree_->selectedItems();
    if (sel.isEmpty() || !sel.first()->parent()) return;
    const QString path = sel.first()->data(0, DupPathRole).toString();
    const bool isDump = sel.first()->data(0, Qt::UserRole).toBool();
    QDesktopServices::openUrl(QUrl::fromLocalFile(isDump ? path : QFileInfo(path).absolutePath()));
}

void DuplicatesDialog::trashChecked()
{
    QList<QTreeWidgetItem *> marked;
    qint64 bytes = 0;
    for (int g = 0; g < tree_->topLevelItemCount(); ++g) {
        QTreeWidgetItem *grp = tree_->topLevelItem(g);
        int checked = 0;
        for (int k = 0; k < grp->childCount(); ++k) checked += grp->child(k)->checkState(0) == Qt::Checked;
        if (checked == grp->childCount() && checked > 0) {          // нельзя отправить в Корзину все копии сразу
            QMessageBox::warning(this, TR("app.name"), TR("dup.allMarked"));
            return;
        }
        for (int k = 0; k < grp->childCount(); ++k)
            if (grp->child(k)->checkState(0) == Qt::Checked) {
                marked << grp->child(k);
                bytes += grp->child(k)->data(2, Qt::UserRole).toLongLong();
            }
    }
    if (marked.isEmpty()) {
        QMessageBox::information(this, TR("app.name"), TR("dup.nothingMarked"));
        return;
    }
    if (QMessageBox::question(this, TR("app.name"), I18n::instance().t("dup.confirm", {int(marked.size()), fmtBytes(bytes)})) != QMessageBox::Yes)
        return;

    int ok = 0;
    QStringList failed;
    for (QTreeWidgetItem *it : std::as_const(marked)) {
        const QString path = it->data(0, DupPathRole).toString();
        if (QFile::moveToTrash(path)) { ++ok; it->setCheckState(0, Qt::Unchecked); it->setDisabled(true); it->setText(0, it->text(0) + QStringLiteral("  ✓")); }
        else failed << path;
    }
    if (ok) changed_ = true;
    QString msg = I18n::instance().t("dup.done", {ok});
    if (!failed.isEmpty()) msg += QLatin1Char('\n') + I18n::instance().t("dup.failed", {failed.join(QLatin1Char('\n'))});
    QMessageBox::information(this, TR("app.name"), msg);
}

// ================================================================= переименование

static QString categoryOf(const QString &role)
{
    if (role == QLatin1String("base")) return QStringLiteral("Game");
    if (role == QLatin1String("update") || role == QLatin1String("update-old")) return QStringLiteral("Patch");
    if (role == QLatin1String("dlc")) return QStringLiteral("DLC");
    if (role == QLatin1String("app")) return QStringLiteral("App");
    return QString();
}

static QString sanitizeName(QString s)
{
    static const QRegularExpression bad(QStringLiteral(R"([<>:"/\\|?*\x00-\x1f])"));
    s.replace(bad, QStringLiteral("_"));
    // пустые токены оставляют «[]» и двойные пробелы — убираем
    s.remove(QRegularExpression(QStringLiteral(R"(\[\s*\])")));
    s.replace(QRegularExpression(QStringLiteral(R"(\s{2,})")), QStringLiteral(" "));
    s = s.trimmed();
    while (s.endsWith(QLatin1Char('.')) || s.endsWith(QLatin1Char(' '))) s.chop(1);
    if (s.size() > 150) s = s.left(150).trimmed();
    return s;
}

RenameDialog::RenameDialog(const QList<LibraryItem> &items, QWidget *parent) : QDialog(parent), items_(items)
{
    setWindowTitle(TR("ren.title"));
    resize(1000, 560);
    auto *v = new QVBoxLayout(this);
    auto *hint = new QLabel(TR("ren.hint"));
    hint->setWordWrap(true);
    hint->setObjectName("muted");
    v->addWidget(hint);

    format_ = new QComboBox;
    format_->setEditable(true);
    format_->addItems({QStringLiteral("{TITLE} [{TITLE_ID}] [{VERSION}]"), QStringLiteral("{TITLE} [{TITLE_ID}]"),
                       QStringLiteral("{PLATFORM} {TITLE} [{TITLE_ID}]"), QStringLiteral("{TITLE}"),
                       QStringLiteral("{TITLE_ID}"), QStringLiteral("{TITLE_ID} [{TITLE}]"),
                       QStringLiteral("[{TITLE_ID}] [{CATEGORY}] [{VERSION}] {TITLE}"),
                       QStringLiteral("{TITLE} [{CATEGORY}] [{VERSION}]"), QStringLiteral("{TITLE} [{REGION}] [{CATEGORY}]"),
                       QStringLiteral("{CONTENT_ID}"), QStringLiteral("{CONTENT_ID} [{VERSION}]"),
                       QStringLiteral("{TITLE} [{TITLE_ID}] [{CATEGORY}] [FW {FW}]")});
    v->addWidget(format_);

    info_ = new QLabel;
    info_->setObjectName("muted");
    v->addWidget(info_);

    table_ = new QTableWidget(0, 3);
    table_->setHorizontalHeaderLabels({TR("ren.col.old"), TR("ren.col.new"), TR("ren.col.status")});
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setShowGrid(false);
    table_->verticalHeader()->hide();
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->setColumnWidth(0, 380);
    table_->setColumnWidth(1, 380);
    v->addWidget(table_, 1);

    auto *box = new QDialogButtonBox;
    auto *ok = box->addButton(TR("ren.apply"), QDialogButtonBox::AcceptRole);
    auto *close = box->addButton(TR("dup.close"), QDialogButtonBox::RejectRole);
    v->addWidget(box);
    connect(ok, &QPushButton::clicked, this, &RenameDialog::apply);
    connect(close, &QPushButton::clicked, this, &QDialog::accept);
    connect(format_, &QComboBox::editTextChanged, this, &RenameDialog::updatePreview);
    updatePreview();
}

QString RenameDialog::newBaseName(const LibraryItem &it) const
{
    QString s = format_->currentText();
    const QHash<QString, QString> tokens = {
        {QStringLiteral("{TITLE}"), it.title},       {QStringLiteral("{TITLE_ID}"), it.titleId},
        {QStringLiteral("{CONTENT_ID}"), it.contentId}, {QStringLiteral("{VERSION}"), it.version},
        {QStringLiteral("{CATEGORY}"), categoryOf(it.role)}, {QStringLiteral("{REGION}"), it.region},
        {QStringLiteral("{PLATFORM}"), it.platform}, {QStringLiteral("{FW}"), it.requiredFw}, {QStringLiteral("{SDK}"), it.sdk}};
    for (auto t = tokens.cbegin(); t != tokens.cend(); ++t) s.replace(t.key(), t.value(), Qt::CaseInsensitive);
    return sanitizeName(s);
}

void RenameDialog::updatePreview()
{
    table_->setRowCount(0);
    QSet<QString> taken;                              // имена, которые получатся в этом же проходе
    int will = 0;
    for (int i = 0; i < items_.size(); ++i) {
        const LibraryItem &it = items_.at(i);
        const QFileInfo fi(it.path);
        const QString suffix = it.isDump ? QString() : (fi.suffix().isEmpty() ? QString() : QLatin1Char('.') + fi.suffix());
        const QString base = newBaseName(it);
        const QString newName = base + suffix;
        const QString target = fi.absoluteDir().absoluteFilePath(newName);

        QString status;
        bool ok = true;
        if (base.isEmpty()) { status = TR("ren.st.empty"); ok = false; }
        else if (newName == fi.fileName()) { status = TR("ren.st.same"); ok = false; }
        else if (QFileInfo::exists(target) && target.compare(fi.absoluteFilePath(), Qt::CaseInsensitive) != 0) { status = TR("ren.st.exists"); ok = false; }
        else if (taken.contains(target.toLower())) { status = TR("ren.st.conflict"); ok = false; }
        else status = TR("ren.st.ok");
        if (ok) { taken.insert(target.toLower()); ++will; }

        const int r = table_->rowCount();
        table_->insertRow(r);
        auto *a = new QTableWidgetItem(fi.fileName());
        a->setData(Qt::UserRole, i);
        a->setToolTip(it.path);
        auto *b = new QTableWidgetItem(newName);
        auto *c = new QTableWidgetItem(status);
        c->setData(Qt::UserRole, ok);
        c->setForeground(ok ? QColor(QStringLiteral("#3ecf8e")) : QColor(QStringLiteral("#98a2b3")));
        table_->setItem(r, 0, a);
        table_->setItem(r, 1, b);
        table_->setItem(r, 2, c);
    }
    info_->setText(I18n::instance().t("ren.summary", {will, int(items_.size())}));
}

void RenameDialog::apply()
{
    QList<int> rows;
    for (int r = 0; r < table_->rowCount(); ++r)
        if (table_->item(r, 2)->data(Qt::UserRole).toBool()) rows << r;
    if (rows.isEmpty()) { QMessageBox::information(this, TR("app.name"), TR("ren.nothing")); return; }
    if (QMessageBox::question(this, TR("app.name"), I18n::instance().t("ren.confirm", {int(rows.size())})) != QMessageBox::Yes) return;

    int done = 0;
    QStringList failed;
    for (int r : std::as_const(rows)) {
        const LibraryItem &it = items_.at(table_->item(r, 0)->data(Qt::UserRole).toInt());
        const QFileInfo fi(it.path);
        const QString target = fi.absoluteDir().absoluteFilePath(table_->item(r, 1)->text());
        const bool ok = it.isDump ? QDir().rename(fi.absoluteFilePath(), target) : QFile::rename(fi.absoluteFilePath(), target);
        if (ok) { ++done; table_->item(r, 2)->setText(TR("ren.st.done")); table_->item(r, 2)->setData(Qt::UserRole, false); }
        else { failed << fi.fileName(); table_->item(r, 2)->setText(TR("ren.st.failed")); }
    }
    if (done) changed_ = true;
    QString msg = I18n::instance().t("ren.done", {done});
    if (!failed.isEmpty()) msg += QLatin1Char('\n') + I18n::instance().t("ren.failedList", {failed.join(QStringLiteral(", "))});
    QMessageBox::information(this, TR("app.name"), msg);
}
