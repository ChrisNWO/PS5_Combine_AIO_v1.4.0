#include "LibraryPage.h"

#include <QApplication>
#include <QFont>
#include <QMessageBox>
#include <QSaveFile>
#include <QTreeView>
#include <functional>
#include <QClipboard>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMenu>
#include <QPixmap>
#include <QRegularExpression>
#include <QSet>
#include <QSortFilterProxyModel>
#include <QStandardPaths>
#include <QUrl>
#include <QVBoxLayout>

#include "AppPaths.h"
#include "CompareDialog.h"
#include "GameCheck.h"
#include "LibraryDialogs.h"
#include "ParamJson.h"
#include "ToolRunner.h"

// ================================================================= вспомогательное

static int rolePriority(const QString &r)
{
    if (r == QLatin1String("base")) return 0;
    if (r == QLatin1String("update")) return 1;
    if (r == QLatin1String("update-old")) return 2;
    if (r == QLatin1String("dlc")) return 3;
    if (r == QLatin1String("app")) return 4;
    return 5;
}

static QList<int> versionParts(const QString &v)
{
    QList<int> parts;
    const QStringList tokens = v.split(QRegularExpression(QStringLiteral("[^0-9]+")), Qt::SkipEmptyParts);
    for (const QString &t : tokens) parts << t.toInt();
    return parts;
}

// -1 / 0 / +1: версия a меньше / равна / больше b ("01.002.000" vs "01.010.000")
static int cmpVersion(const QString &a, const QString &b)
{
    const QList<int> pa = versionParts(a), pb = versionParts(b);
    const int n = qMax(pa.size(), pb.size());
    for (int i = 0; i < n; ++i) {
        const int x = i < pa.size() ? pa.at(i) : 0, y = i < pb.size() ? pb.at(i) : 0;
        if (x != y) return x < y ? -1 : 1;
    }
    return 0;
}

// Роль в семействе — та же логика, что у PS5 PKG Tool (applicationCategoryType: 1 = DLC, 2 = патч, 3 = приложение).
static QString roleFromGame(const QJsonObject &g)
{
    const QString raw = g.value(QLatin1String("ApplicationCategory")).toString().trimmed();
    if (!raw.isEmpty()) {
        bool ok = false;
        const int n = raw.section(QLatin1Char(' '), 0, 0).toInt(&ok);
        if (ok) return n == 1 ? QStringLiteral("dlc") : n == 2 ? QStringLiteral("update") : n == 3 ? QStringLiteral("app") : QStringLiteral("base");
        const QString l = raw.toLower();
        if (l == QLatin1String("game")) return QStringLiteral("base");
        if (l == QLatin1String("patch")) return QStringLiteral("update");
        if (l == QLatin1String("dlc") || l == QLatin1String("add-on")) return QStringLiteral("dlc");
        if (l == QLatin1String("app")) return QStringLiteral("app");
        return QStringLiteral("unknown");
    }
    const int ct = g.value(QLatin1String("Package")).toObject().value(QLatin1String("ContentType")).toInt(-1);
    if (ct == 0x20) return QStringLiteral("base");
    if (ct == 0x21 || ct == 0x22) return QStringLiteral("dlc");
    return g.value(QLatin1String("SourceKind")).toInt() == 0 ? QStringLiteral("base") : QStringLiteral("unknown");
}

// Сумма размеров файлов папки-дампа. Сканер PS5 PKG Tool для дампов размер не считает (это откладывается до загрузки
// подробностей), поэтому без этого в списке было бы «0 Б». Читаются только записи каталога, содержимое файлов не открывается.
static qint64 dirBytes(const QString &root)
{
    qint64 total = 0;
    QDirIterator it(root, QDir::Files | QDir::Hidden | QDir::System | QDir::NoSymLinks, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        total += it.fileInfo().size();
    }
    return total;
}

// Описание игры от ps5aio-helper (сериализованный Ps5GameInfo) -> элемент таблицы.
static LibraryItem itemFromGame(const QJsonObject &g)
{
    static const char *codes[] = {"dump", "pkg", "ffpfsc", "exfat", "ffpkg"};
    LibraryItem it;
    const int kind = g.value(QLatin1String("SourceKind")).toInt();
    it.source = QString::fromLatin1(kind >= 0 && kind < 5 ? codes[kind] : "dump");
    it.isDump = (kind == 0);
    const QString root = g.value(QLatin1String("RootPath")).toString();
    it.path = QDir::toNativeSeparators(root);
    it.fileName = QFileInfo(root).fileName();
    it.title = g.value(QLatin1String("Title")).toString().trimmed();
    if (it.title.isEmpty()) it.title = QFileInfo(root).completeBaseName().isEmpty() ? it.fileName : QFileInfo(root).completeBaseName();
    it.titleId = g.value(QLatin1String("TitleId")).toString();
    it.contentId = g.value(QLatin1String("ContentId")).toString();
    it.version = g.value(QLatin1String("DisplayVersion")).toString();
    if (it.version.isEmpty()) it.version = g.value(QLatin1String("ContentVersion")).toString();
    if (it.version.isEmpty()) it.version = g.value(QLatin1String("MasterVersion")).toString();
    it.requiredFw = g.value(QLatin1String("RequiredSystemSoftware")).toString();
    it.sdk = g.value(QLatin1String("SdkVersion")).toString();
    it.drm = g.value(QLatin1String("DrmType")).toString();
    it.sourceDetail = g.value(QLatin1String("SourceDescription")).toString();
    it.region = regionFromContentId(it.contentId);
    it.platform = platformFromIds(it.titleId, it.contentId);
    const qint64 a = qint64(g.value(QLatin1String("GameRootBytes")).toDouble());
    const qint64 b = qint64(g.value(QLatin1String("SourceSize")).toDouble());
    const qint64 c = qint64(g.value(QLatin1String("ContainerFileLength")).toDouble());
    it.size = a > 0 ? a : b > 0 ? b : c;
    if (it.size <= 0 && it.isDump) it.size = dirBytes(root);
    it.role = roleFromGame(g);
    it.gameJson = QJsonDocument(g).toJson(QJsonDocument::Compact);
    return it;
}

// ================================================================= фильтр + группировка

// Модель — либо плоский список, либо дерево «группа → элементы». Строки групп в фильтре не участвуют сами:
// рекурсивная фильтрация показывает группу, только если в ней виден хотя бы один элемент.
class LibraryProxy : public QSortFilterProxyModel
{
public:
    explicit LibraryProxy(QObject *parent = nullptr) : QSortFilterProxyModel(parent) { setRecursiveFilteringEnabled(true); }
    const QList<LibraryItem> *items = nullptr;

    void setFilters(const QString &t, const QString &s, const QString &r)
    {
        text = t.toLower().split(QLatin1Char(' '), Qt::SkipEmptyParts);
        source = s;
        region = r;
        invalidateFilter();
    }

    void setGroupMode(const QString &mode) { groupMode = mode; }

protected:
    bool filterAcceptsRow(int row, const QModelIndex &parent) const override
    {
        const LibraryItem *it = itemAt(sourceModel()->index(row, 0, parent));
        if (!it) return false;                     // строка группы
        if (!source.isEmpty() && it->source != source) return false;
        if (!region.isEmpty() && it->region != region) return false;
        if (text.isEmpty()) return true;
        const QString hay = (it->title + QLatin1Char(' ') + it->titleId + QLatin1Char(' ') + it->contentId + QLatin1Char(' ')
                             + it->fileName + QLatin1Char(' ') + it->path).toLower();
        for (const QString &tok : text) {
            if (tok.size() > 1 && tok.startsWith(QLatin1Char('-'))) {
                if (hay.contains(tok.mid(1))) return false;
            } else if (!hay.contains(tok)) {
                return false;
            }
        }
        return true;
    }

    // Группы всегда по алфавиту (по ключу группы); внутри группы при сортировке по «Названию» — роль, потом версия (новее выше).
    bool lessThan(const QModelIndex &l, const QModelIndex &r) const override
    {
        if (!groupMode.isEmpty() && items) {
            const bool lTop = !l.parent().isValid(), rTop = !r.parent().isValid();
            if (lTop && rTop)
                return sourceModel()->index(l.row(), 0).data(Qt::UserRole).toString()
                       < sourceModel()->index(r.row(), 0).data(Qt::UserRole).toString();
            if (!lTop && !rTop && sortColumn() == 0) {
                const LibraryItem *a = itemAt(l), *b = itemAt(r);
                if (a && b) {
                    const int pa = rolePriority(a->role), pb = rolePriority(b->role);
                    if (pa != pb) return pa < pb;
                    const int v = cmpVersion(a->version, b->version);
                    if (v != 0) return v > 0;
                    return a->title.compare(b->title, Qt::CaseInsensitive) < 0;
                }
            }
        }
        return QSortFilterProxyModel::lessThan(l, r);
    }

private:
    const LibraryItem *itemAt(const QModelIndex &sourceIndex) const
    {
        if (!items || !sourceIndex.isValid()) return nullptr;
        const int idx = sourceModel()->index(sourceIndex.row(), 0, sourceIndex.parent()).data(Qt::UserRole + 1).toInt();
        return (idx >= 0 && idx < items->size()) ? &items->at(idx) : nullptr;
    }

    QStringList text;
    QString source, region, groupMode;
};

static QTableWidget *makeTable(int cols)
{
    auto *t = new QTableWidget(0, cols);
    t->setEditTriggers(QAbstractItemView::NoEditTriggers);
    t->setSelectionBehavior(QAbstractItemView::SelectRows);
    t->setShowGrid(false);
    t->verticalHeader()->hide();
    t->horizontalHeader()->setStretchLastSection(true);
    return t;
}

static void fillTable(QTableWidget *t, const QStringList &headers, const QList<QStringList> &rows)
{
    t->setUpdatesEnabled(false);
    t->clear();
    t->setColumnCount(headers.size());
    t->setHorizontalHeaderLabels(headers);
    t->setRowCount(rows.size());
    for (int r = 0; r < rows.size(); ++r)
        for (int c = 0; c < headers.size() && c < rows.at(r).size(); ++c) {
            auto *item = new QTableWidgetItem(rows.at(r).at(c));
            item->setToolTip(rows.at(r).at(c));
            t->setItem(r, c, item);
        }
    t->resizeColumnsToContents();
    for (int c = 0; c < headers.size(); ++c)
        if (t->columnWidth(c) > 420) t->setColumnWidth(c, 420);
    t->setUpdatesEnabled(true);
}

// ================================================================= страница

LibraryPage::LibraryPage(QWidget *parent) : PageBase(parent)
{
    qRegisterMetaType<LibraryItem>("LibraryItem");

    auto *page = new QVBoxLayout(this);
    page->setContentsMargins(28, 24, 28, 12);
    page->setSpacing(12);
    header(page, "lib.title", "lib.subtitle");

    auto *bar = new QHBoxLayout;
    bAdd_ = R(new QPushButton, "lib.add");
    bRemove_ = R(new QPushButton, "lib.remove");
    bRescan_ = R(new QPushButton, "lib.rescan");
    bDup_ = R(new QPushButton, "lib.dup");
    bRename_ = R(new QPushButton, "lib.rename");
    bExport_ = R(new QPushButton, "lib.export");
    bCompare_ = R(new QPushButton, "lib.compare");
    bExpand_ = R(new QPushButton, "lib.expandAll");
    bCollapse_ = R(new QPushButton, "lib.collapseAll");
    foldersLbl_ = new QLabel;
    foldersLbl_->setObjectName("muted");
    bar->addWidget(bAdd_);
    bar->addWidget(bRemove_);
    bar->addWidget(bRescan_);
    bar->addWidget(foldersLbl_, 1);
    bar->addWidget(bExpand_);
    bar->addWidget(bCollapse_);
    bar->addWidget(bCompare_);
    bar->addWidget(bDup_);
    bar->addWidget(bRename_);
    bar->addWidget(bExport_);
    page->addLayout(bar);

    auto *flt = new QHBoxLayout;
    search_ = new QLineEdit;
    search_->setClearButtonEnabled(true);
    srcFilter_ = new QComboBox;
    regFilter_ = new QComboBox;
    groupBy_ = new QComboBox;
    fillCombo(groupBy_, {{"", "lib.group.none"}, {"family", "lib.group.family"}, {"region", "lib.group.region"},
                         {"source", "lib.group.source"}, {"fw", "lib.group.fw"}});
    countLbl_ = new QLabel;
    countLbl_->setObjectName("muted");
    flt->addWidget(search_, 1);
    flt->addWidget(srcFilter_);
    flt->addWidget(regFilter_);
    flt->addWidget(groupBy_);
    flt->addWidget(countLbl_);
    page->addLayout(flt);

    model_ = new QStandardItemModel(0, CCount, this);
    proxy_ = new LibraryProxy(this);
    proxy_->items = &items_;
    proxy_->setSourceModel(model_);
    proxy_->setSortRole(Qt::UserRole);
    table_ = new QTreeView;
    table_->setModel(proxy_);
    table_->setSortingEnabled(true);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setUniformRowHeights(true);
    table_->setRootIsDecorated(false);
    table_->setAllColumnsShowFocus(true);
    table_->header()->setStretchLastSection(true);
    table_->header()->setSectionsMovable(true);
    table_->setContextMenuPolicy(Qt::CustomContextMenu);
    table_->setMinimumHeight(260);
    page->addWidget(table_, 2);

    status_ = new QLabel;
    status_->setObjectName("muted");
    page->addWidget(status_);

    // ---------- вкладки подробностей ----------
    tabs_ = new QTabWidget;
    tabs_->setMinimumHeight(250);

    auto *ov = new QWidget;               // Обзор
    auto *ovl = new QHBoxLayout(ov);
    cover_ = new QLabel;
    cover_->setFixedSize(132, 132);
    cover_->setAlignment(Qt::AlignCenter);
    cover_->setObjectName("muted");
    detail_ = new QLabel;
    detail_->setWordWrap(true);
    detail_->setTextFormat(Qt::RichText);
    detail_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    detail_->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    ovl->addWidget(cover_);
    ovl->addWidget(detail_, 1);
    tabs_->addTab(ov, QString());

    auto *aw = new QWidget;               // Обложки
    auto *al = new QHBoxLayout(aw);
    for (int i = 0; i < 4; ++i) {
        art_[i] = new QLabel;
        art_[i]->setAlignment(Qt::AlignCenter);
        art_[i]->setMinimumSize(170, 110);
        art_[i]->setObjectName("muted");
        al->addWidget(art_[i], 1);
    }
    tabs_->addTab(aw, QString());

    auto *tw = new QWidget;               // Трофеи
    auto *tl = new QVBoxLayout(tw);
    trophyInfo_ = new QLabel;
    trophyInfo_->setObjectName("muted");
    trophyInfo_->setWordWrap(true);
    trophyTable_ = makeTable(6);
    trophyTable_->setIconSize(QSize(36, 36));
    tl->addWidget(trophyInfo_);
    tl->addWidget(trophyTable_, 1);
    tabs_->addTab(tw, QString());

    auto *uw = new QWidget;               // Activities & UDS
    auto *ul = new QVBoxLayout(uw);
    udsInfo_ = new QLabel;
    udsInfo_->setObjectName("muted");
    udsInfo_->setWordWrap(true);
    udsTabs_ = new QTabWidget;
    evTable_ = makeTable(4);
    statTable_ = makeTable(9);
    enumTable_ = makeTable(3);
    ruleTable_ = makeTable(7);
    udsTabs_->addTab(evTable_, QString());
    udsTabs_->addTab(statTable_, QString());
    udsTabs_->addTab(enumTable_, QString());
    udsTabs_->addTab(ruleTable_, QString());
    ul->addWidget(udsInfo_);
    ul->addWidget(udsTabs_, 1);
    tabs_->addTab(uw, QString());

    auto *fw = new QWidget;               // Файлы
    auto *fl = new QVBoxLayout(fw);
    auto *fbar = new QHBoxLayout;
    filesFilter_ = new QLineEdit;
    filesFilter_->setClearButtonEnabled(true);
    bExtract_ = R(new QPushButton, "lib.files.extract");
    fbar->addWidget(filesFilter_, 1);
    fbar->addWidget(bExtract_);
    filesInfo_ = new QLabel;
    filesInfo_->setObjectName("muted");
    filesModel_ = new QStandardItemModel(0, 3, this);
    filesProxy_ = new QSortFilterProxyModel(this);
    filesProxy_->setSourceModel(filesModel_);
    filesProxy_->setFilterKeyColumn(0);
    filesProxy_->setFilterCaseSensitivity(Qt::CaseInsensitive);
    filesProxy_->setSortRole(Qt::UserRole);
    filesView_ = new QTableView;
    filesView_->setModel(filesProxy_);
    filesView_->setSortingEnabled(true);
    filesView_->setSelectionBehavior(QAbstractItemView::SelectRows);
    filesView_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    filesView_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    filesView_->setShowGrid(false);
    filesView_->verticalHeader()->hide();
    filesView_->horizontalHeader()->setStretchLastSection(true);
    fl->addLayout(fbar);
    fl->addWidget(filesInfo_);
    fl->addWidget(filesView_, 1);
    tabs_->addTab(fw, QString());

    exeView_ = new QPlainTextEdit;        // Исполняемый файл
    exeView_->setReadOnly(true);
    tabs_->addTab(exeView_, QString());

    rawView_ = new QPlainTextEdit;        // param.json
    rawView_->setReadOnly(true);
    tabs_->addTab(rawView_, QString());

    detailStatus_ = new QLabel;
    detailStatus_->setObjectName("muted");
    detailStatus_->setWordWrap(true);
    page->addWidget(detailStatus_);
    page->addWidget(tabs_, 2);

    detailTimer_.setSingleShot(true);
    detailTimer_.setInterval(250);

    connect(bAdd_, &QPushButton::clicked, this, &LibraryPage::addFolder);
    connect(bRemove_, &QPushButton::clicked, this, &LibraryPage::removeFolder);
    connect(bRescan_, &QPushButton::clicked, this, &LibraryPage::startScan);
    connect(search_, &QLineEdit::textChanged, this, &LibraryPage::applyFilter);
    connect(srcFilter_, &QComboBox::currentIndexChanged, this, &LibraryPage::applyFilter);
    connect(regFilter_, &QComboBox::currentIndexChanged, this, &LibraryPage::applyFilter);
    connect(groupBy_, &QComboBox::currentIndexChanged, this, [this] { rebuildModel(); });
    connect(bExpand_, &QPushButton::clicked, table_, &QTreeView::expandAll);
    connect(bCollapse_, &QPushButton::clicked, table_, &QTreeView::collapseAll);
    connect(bDup_, &QPushButton::clicked, this, &LibraryPage::findDuplicates);
    connect(bCompare_, &QPushButton::clicked, this, &LibraryPage::compareImages);
    connect(bRename_, &QPushButton::clicked, this, &LibraryPage::renameItems);
    connect(bExport_, &QPushButton::clicked, this, &LibraryPage::exportList);
    connect(table_->selectionModel(), &QItemSelectionModel::currentRowChanged, this, [this] { onSelection(); });
    connect(table_, &QTreeView::customContextMenuRequested, this, &LibraryPage::showMenu);
    connect(table_, &QTreeView::doubleClicked, this, [this] {
        if (const LibraryItem *it = currentItem())
            QDesktopServices::openUrl(QUrl::fromLocalFile(it->isDump ? it->path : QFileInfo(it->path).absolutePath()));
    });
    connect(&detailTimer_, &QTimer::timeout, this, [this] {
        if (const LibraryItem *it = currentItem()) loadDetails(*it);
    });
    connect(tabs_, &QTabWidget::currentChanged, this, [this](int idx) {
        if (idx == TFiles)
            if (const LibraryItem *it = currentItem()) loadFiles(*it);
    });
    connect(filesFilter_, &QLineEdit::textChanged, this, [this](const QString &t) { filesProxy_->setFilterFixedString(t); });
    connect(bExtract_, &QPushButton::clicked, this, &LibraryPage::extractSelectedFiles);

    onRetranslate();
    if (!folders().isEmpty())
        QTimer::singleShot(150, this, &LibraryPage::startScan);
}

LibraryPage::~LibraryPage()
{
    stopScans();
    for (ToolRunner *r : {detailRunner_, filesRunner_})
        if (r) r->cancel();
    QDir(tempBase()).removeRecursively();
}

QStringList LibraryPage::folders() const { return AppPaths::settings().value("library/folders").toStringList(); }

QString LibraryPage::tempBase() const
{
    return QStandardPaths::writableLocation(QStandardPaths::TempLocation) + QStringLiteral("/PS5CombineAIO");
}

void LibraryPage::showEvent(QShowEvent *e)
{
    PageBase::showEvent(e);
    refreshCompat();       // пользователь мог поменять прошивку консоли в «Настройках»
}

// ================================================================= тексты

QString LibraryPage::sourceCodeText(const QString &code) const
{
    const QString key = QStringLiteral("lib.src.") + code;
    const QString t = I18n::instance().t(key);
    return t == key ? code.toUpper() : t;
}

QString LibraryPage::sourceText(const LibraryItem &it) const
{
    // Для пакетов helper знает точный вид: Debug PKG / Retail PKG.
    if (it.source == QLatin1String("pkg") && !it.sourceDetail.isEmpty()) return it.sourceDetail;
    return sourceCodeText(it.source);
}

QString LibraryPage::regionText(const QString &code) const
{
    if (code.isEmpty()) return QStringLiteral("—");
    const QString key = QStringLiteral("region.") + code;
    const QString t = I18n::instance().t(key);
    return t == key ? code : t;
}

QString LibraryPage::roleText(const QString &role) const
{
    const QString key = QStringLiteral("lib.role.") + role;
    const QString t = I18n::instance().t(key);
    return t == key ? QStringLiteral("—") : t;
}

QString LibraryPage::fmtSize(qint64 b) const
{
    auto &i = I18n::instance();
    const double v = double(b);
    if (b >= (1LL << 30)) return QString::number(v / double(1LL << 30), 'f', 1) + QLatin1Char(' ') + i.t("unit.gb");
    if (b >= (1LL << 20)) return QString::number(v / double(1LL << 20), 'f', 1) + QLatin1Char(' ') + i.t("unit.mb");
    if (b >= (1LL << 10)) return QString::number(v / double(1LL << 10), 'f', 0) + QLatin1Char(' ') + i.t("unit.kb");
    return QString::number(b) + QLatin1Char(' ') + i.t("unit.b");
}

void LibraryPage::onRetranslate()
{
    static const char *cols[CCount] = {"lib.col.title", "lib.col.titleId", "lib.col.contentId", "lib.col.role", "lib.col.platform",
                                       "lib.col.region", "lib.col.source", "lib.col.size", "lib.col.version",
                                       "lib.col.fw", "lib.col.file", "lib.col.path"};
    for (int c = 0; c < CCount; ++c)
        model_->setHeaderData(c, Qt::Horizontal, I18n::instance().t(QString::fromLatin1(cols[c])));
    search_->setPlaceholderText(TR("lib.search"));
    rebuildFilters();

    static const char *tabNames[TCount] = {"lib.tab.overview", "lib.tab.artwork", "lib.tab.trophies", "lib.tab.activities",
                                           "lib.tab.files", "lib.tab.exe", "lib.tab.raw"};
    for (int i = 0; i < TCount; ++i) tabs_->setTabText(i, I18n::instance().t(QString::fromLatin1(tabNames[i])));
    static const char *udsNames[4] = {"lib.uds.events", "lib.uds.stats", "lib.uds.enums", "lib.uds.rules"};
    for (int i = 0; i < 4; ++i) udsTabs_->setTabText(i, I18n::instance().t(QString::fromLatin1(udsNames[i])));
    filesFilter_->setPlaceholderText(TR("lib.files.filter"));
    filesModel_->setHorizontalHeaderLabels({TR("lib.files.col.path"), TR("lib.files.col.size"), TR("lib.files.col.origin")});

    if (!items_.isEmpty()) rebuildModel();       // тексты ячеек и подписи групп зависят от языка
    const int n = int(folders().size());
    foldersLbl_->setText(n ? I18n::instance().t("lib.folders", {n}) : TR("lib.noFolders"));
    foldersLbl_->setToolTip(folders().join(QLatin1Char('\n')));
    applyFilter();
    onSelection();
}

void LibraryPage::rebuildFilters()
{
    const QString curSrc = srcFilter_->currentData().toString(), curReg = regFilter_->currentData().toString();
    QSet<QString> srcs, regs;
    for (const LibraryItem &it : std::as_const(items_)) {
        srcs.insert(it.source);
        if (!it.region.isEmpty()) regs.insert(it.region);
    }
    srcFilter_->blockSignals(true);
    regFilter_->blockSignals(true);
    srcFilter_->clear();
    regFilter_->clear();
    srcFilter_->addItem(TR("lib.allSources"), QString());
    regFilter_->addItem(TR("lib.allRegions"), QString());
    QStringList sl = srcs.values(), rl = regs.values();
    sl.sort();
    rl.sort();
    for (const QString &s : std::as_const(sl)) srcFilter_->addItem(sourceCodeText(s), s);
    for (const QString &r : std::as_const(rl)) regFilter_->addItem(regionText(r), r);
    srcFilter_->setCurrentIndex(qMax(0, srcFilter_->findData(curSrc)));
    regFilter_->setCurrentIndex(qMax(0, regFilter_->findData(curReg)));
    srcFilter_->blockSignals(false);
    regFilter_->blockSignals(false);
}

// ================================================================= сканирование

void LibraryPage::stopScans()
{
    if (scanner_) {
        scanner_->cancel();
        scanner_->wait(5000);
        scanner_->deleteLater();
        scanner_ = nullptr;
    }
    if (helperScan_) {
        helperScan_->cancel();
        helperScan_->deleteLater();
        helperScan_ = nullptr;
    }
}

void LibraryPage::startScan()
{
    stopScans();
    table_->setSortingEnabled(false);
    model_->removeRows(0, model_->rowCount());
    rowCells_.clear();
    proxy_->setGroupMode(QString());
    table_->setRootIsDecorated(false);
    items_.clear();
    detailCache_.clear();
    loadedFilesFor_.clear();
    scanErrors_.clear();
    rebuildFilters();
    clearDetails(QString());
    cover_->clear();
    detail_->clear();

    const QStringList roots = folders();
    foldersLbl_->setText(!roots.isEmpty() ? I18n::instance().t("lib.folders", {int(roots.size())}) : TR("lib.noFolders"));
    foldersLbl_->setToolTip(roots.join(QLatin1Char('\n')));
    if (roots.isEmpty()) {
        status_->setText(TR("lib.noFolders"));
        table_->setSortingEnabled(true);
        applyFilter();
        return;
    }
    if (AppPaths::helperAvailable()) startHelperScan(roots);
    else startNativeScan(roots);
}

void LibraryPage::startNativeScan(const QStringList &roots)
{
    scanner_ = new LibraryScanner(roots, this);
    LibraryScanner *sc = scanner_;      // сигналы уже остановленного сканера, оставшиеся в очереди, игнорируем
    connect(sc, &LibraryScanner::itemFound, this, [this, sc](const LibraryItem &it) {
        if (sc == scanner_) addItem(it);
    }, Qt::QueuedConnection);
    connect(sc, &LibraryScanner::scanning, this, [this, sc](const QString &d) {
        if (sc == scanner_) status_->setText(I18n::instance().t("lib.scanning", {QDir::toNativeSeparators(d)}));
    }, Qt::QueuedConnection);
    connect(sc, &QThread::finished, this, [this, sc] {
        if (sc == scanner_) finishScan();
    });
    sc->start();
}

void LibraryPage::startHelperScan(const QStringList &roots)
{
    auto *r = new ToolRunner(this);
    helperScan_ = r;
    QStringList args{QStringLiteral("scan")};
    for (const QString &d : roots) args << QStringLiteral("--folder") << d;
    connect(r, &ToolRunner::logLine, this, [this, r](const QString &line, bool fromStderr) {
        if (r == helperScan_ && !fromStderr) onHelperLine(line);
    });
    connect(r, &ToolRunner::finished, this, [this, r](bool ok, int, const QString &err) {
        if (r != helperScan_) return;
        helperScan_ = nullptr;
        r->deleteLater();
        if (!ok && !err.isEmpty()) scanErrors_ << err;
        finishScan();
    });
    status_->setText(I18n::instance().t("lib.scanning", {roots.first()}));
    r->start(AppPaths::helperExe(), args);
}

void LibraryPage::onHelperLine(const QString &line)
{
    if (!line.startsWith(QLatin1Char('{'))) return;
    QJsonParseError pe;
    const QJsonDocument d = QJsonDocument::fromJson(line.toUtf8(), &pe);
    if (pe.error != QJsonParseError::NoError || !d.isObject()) return;
    const QJsonObject o = d.object();
    const QString type = o.value(QLatin1String("type")).toString();
    if (type == QLatin1String("progress")) {
        status_->setText(I18n::instance().t("lib.scanningN", {o.value(QLatin1String("processed")).toInt(), o.value(QLatin1String("total")).toInt(),
                                                              QDir::toNativeSeparators(o.value(QLatin1String("path")).toString())}));
    } else if (type == QLatin1String("game")) {
        addItem(itemFromGame(o.value(QLatin1String("game")).toObject()));
    } else if (type == QLatin1String("error")) {
        scanErrors_ << o.value(QLatin1String("message")).toString();
    }
}

void LibraryPage::finishScan()
{
    refreshRoles();
    rebuildFilters();
    rebuildModel();
    QString msg = TR("lib.scanDone");
    if (!scanErrors_.isEmpty()) msg += QStringLiteral(" · ") + I18n::instance().t("lib.scanErrors", {int(scanErrors_.size())});
    status_->setText(msg);
    status_->setToolTip(scanErrors_.join(QLatin1Char('\n')));
}

// Практический минимум прошивки = большее из «Треб. прошивка» (param.json) и версии SDK: у игр из дампов param.json часто
// содержит заниженное 1.00, а реально игра собрана под более новый SDK. Верхняя граница («работает только до X») в файлах
// игры не хранится — она относится к загрузчику/эксплойту и задаётся пользователем в «Настройках».
LibraryPage::FwVerdict LibraryPage::fwVerdict(const LibraryItem &it) const
{
    auto &i = I18n::instance();
    FwVerdict v;
    const int req = fwToNumber(it.requiredFw), sdk = fwToNumber(it.sdk);
    v.effMin = qMax(req, sdk);
    if (v.effMin > 0) v.effText = (sdk > req) ? it.sdk : it.requiredFw;
    const QString cons = AppPaths::settings().value("console/fw").toString().trimmed();
    const QString maxFw = AppPaths::settings().value("console/maxfw").toString().trimmed();
    const int have = fwToNumber(cons), top = fwToNumber(maxFw);
    if (v.effMin > 0 && top > 0 && v.effMin > top) {
        v.color = QStringLiteral("#ff6b6b");
        v.message = i.t("lib.fw.needsAboveLoader", {v.effText, maxFw});
    } else if (have > 0 && v.effMin > have) {
        v.color = QStringLiteral("#ff9f43");
        v.message = i.t("lib.fw.tooHigh", {v.effText, cons});
    } else if (have > 0 && top > 0 && have > top) {
        v.color = QStringLiteral("#ff9f43");
        v.message = i.t("lib.fw.aboveLoader", {cons, maxFw});
    } else if (v.effMin > 0 && have > 0) {
        v.message = i.t("lib.fw.ok", {cons});
    } else if (v.effMin > 0) {
        v.message = i.t("lib.fw.noConsole");
    }
    return v;
}

void LibraryPage::styleFw(QStandardItem *cell, const LibraryItem &it) const
{
    const FwVerdict v = fwVerdict(it);
    cell->setForeground(QBrush());
    cell->setToolTip(QString());
    if (!v.color.isEmpty()) {
        cell->setForeground(QColor(v.color));
        cell->setToolTip(v.message);
    }
}

QList<QStandardItem *> LibraryPage::makeRow(int idx) const
{
    const LibraryItem &it = items_.at(idx);
    auto mk = [](const QString &text, const QVariant &sortKey) {
        auto *c = new QStandardItem(text);
        c->setEditable(false);
        c->setData(sortKey, Qt::UserRole);
        return c;
    };
    auto low = [](const QString &s) { return QVariant(s.toLower()); };

    QList<QStandardItem *> row;
    row << mk(it.title, low(it.title)) << mk(it.titleId, low(it.titleId)) << mk(it.contentId, low(it.contentId))
        << mk(roleText(it.role), rolePriority(it.role)) << mk(it.platform, low(it.platform))
        << mk(regionText(it.region), low(it.region)) << mk(sourceText(it), low(it.source))
        << mk(fmtSize(it.size), QVariant::fromValue<qint64>(it.size)) << mk(it.version, low(it.version))
        << mk(it.requiredFw.isEmpty() ? QStringLiteral("—") : it.requiredFw, fwToNumber(it.requiredFw))
        << mk(it.fileName, low(it.fileName)) << mk(it.path, low(it.path));
    row[CTitle]->setData(idx, Qt::UserRole + 1);
    styleFw(row[CFw], it);
    if (it.sourceDetail.contains(QLatin1String("Retail"), Qt::CaseInsensitive)) {      // retail-пакет: инструменты его не читают
        row[CSource]->setForeground(QColor(QStringLiteral("#ff9f43")));
        row[CSource]->setToolTip(TR("hint.retail"));
    }
    return row;
}

// Во время сканирования элементы идут плоским списком; в дерево по группам модель перестраивается по окончании.
void LibraryPage::addItem(const LibraryItem &it)
{
    items_.append(it);
    const int idx = int(items_.size()) - 1;
    const QList<QStandardItem *> cells = makeRow(idx);
    rowCells_.append(cells);
    model_->appendRow(cells);

    if (srcFilter_->findData(it.source) < 0) srcFilter_->addItem(sourceCodeText(it.source), it.source);
    if (!it.region.isEmpty() && regFilter_->findData(it.region) < 0) regFilter_->addItem(regionText(it.region), it.region);
    countLbl_->setText(I18n::instance().t("lib.status", {int(visibleIndexes().size()), int(items_.size())}));
}

QString LibraryPage::groupKeyFor(const QString &mode, const LibraryItem &it) const
{
    if (mode == QLatin1String("family")) return it.titleId.isEmpty() ? it.title : it.titleId;
    if (mode == QLatin1String("region")) return it.region.isEmpty() ? QStringLiteral("?") : it.region;
    if (mode == QLatin1String("source")) return it.source;
    if (mode == QLatin1String("fw")) return QStringLiteral("%1").arg(qMax(0, fwVerdict(it).effMin), 5, 10, QLatin1Char('0'));
    return QString();
}

QString LibraryPage::groupLabel(const QString &mode, const QList<int> &members) const
{
    const LibraryItem &first = items_.at(members.first());
    QString label;
    if (mode == QLatin1String("family")) {
        const LibraryItem *pick = &first;                       // подпись берём у базовой игры, если она есть
        for (int i : members) if (items_.at(i).role == QLatin1String("base")) { pick = &items_.at(i); break; }
        label = pick->titleId.isEmpty() ? pick->title : QStringLiteral("%1  [%2]").arg(pick->title, pick->titleId);
    } else if (mode == QLatin1String("region")) {
        label = regionText(first.region);
    } else if (mode == QLatin1String("source")) {
        label = sourceCodeText(first.source);
    } else if (mode == QLatin1String("fw")) {
        const FwVerdict v = fwVerdict(first);
        label = v.effText.isEmpty() ? QStringLiteral("FW —") : QStringLiteral("FW ") + v.effText;
    }
    return label + QStringLiteral("  ·  ") + I18n::instance().t("lib.group.count", {int(members.size())});
}

// Индексы items_ в порядке отображения (учитывают фильтр и сортировку; строки групп пропускаются).
QList<int> LibraryPage::visibleIndexes() const
{
    QList<int> out;
    std::function<void(const QModelIndex &)> walk = [&](const QModelIndex &parent) {
        for (int r = 0; r < proxy_->rowCount(parent); ++r) {
            const QModelIndex ix = proxy_->index(r, 0, parent);
            if (proxy_->hasChildren(ix)) { walk(ix); continue; }
            const int idx = ix.data(Qt::UserRole + 1).toInt();
            if (idx >= 0 && idx < items_.size()) out << idx;
        }
    };
    walk(QModelIndex());
    return out;
}

void LibraryPage::rebuildModel()
{
    const QString mode = code(groupBy_);
    QString keepPath;
    if (const LibraryItem *cur = currentItem()) keepPath = cur->path;

    table_->setUpdatesEnabled(false);
    table_->setSortingEnabled(false);
    proxy_->setGroupMode(mode);
    model_->removeRows(0, model_->rowCount());
    rowCells_.clear();
    rowCells_.resize(items_.size());

    QHash<QString, QStandardItem *> groups;
    QHash<QString, QList<int>> members;
    for (int idx = 0; idx < items_.size(); ++idx) {
        const QList<QStandardItem *> cells = makeRow(idx);
        rowCells_[idx] = cells;
        if (mode.isEmpty()) { model_->appendRow(cells); continue; }
        const QString key = groupKeyFor(mode, items_.at(idx));
        QStandardItem *g = groups.value(key, nullptr);
        if (!g) {
            g = new QStandardItem;
            g->setEditable(false);
            g->setData(-1, Qt::UserRole + 1);                  // не элемент библиотеки
            g->setData(key.toLower(), Qt::UserRole);           // ключ сортировки групп
            QFont f = g->font();
            f.setBold(true);
            g->setFont(f);
            model_->appendRow(g);
            groups.insert(key, g);
        }
        g->appendRow(cells);
        members[key].append(idx);
    }
    for (auto it = groups.cbegin(); it != groups.cend(); ++it)
        it.value()->setText(groupLabel(mode, members.value(it.key())));

    table_->setRootIsDecorated(!mode.isEmpty());
    table_->setSortingEnabled(true);
    table_->sortByColumn(CTitle, Qt::AscendingOrder);
    if (!mode.isEmpty()) table_->expandAll();
    for (int c = 0; c < CCount; ++c) {
        table_->resizeColumnToContents(c);
        if (table_->columnWidth(c) > 360) table_->setColumnWidth(c, 360);
    }
    table_->setUpdatesEnabled(true);
    applyFilter();

    if (!keepPath.isEmpty())
        for (int idx = 0; idx < items_.size(); ++idx)
            if (items_.at(idx).path == keepPath && !rowCells_.at(idx).isEmpty()) {
                const QModelIndex p = proxy_->mapFromSource(rowCells_.at(idx).first()->index());
                if (p.isValid()) table_->setCurrentIndex(p);
                break;
            }
}

// Патч, для которого есть более новый патч того же Title ID, помечается «устарел».
void LibraryPage::refreshRoles()
{
    QHash<QString, QString> best;
    for (const LibraryItem &it : std::as_const(items_)) {
        if (it.titleId.isEmpty() || (it.role != QLatin1String("update") && it.role != QLatin1String("update-old"))) continue;
        const QString cur = best.value(it.titleId);
        if (cur.isEmpty() || cmpVersion(it.version, cur) > 0) best.insert(it.titleId, it.version);
    }
    for (LibraryItem &it : items_) {
        if (it.titleId.isEmpty() || (it.role != QLatin1String("update") && it.role != QLatin1String("update-old"))) continue;
        it.role = cmpVersion(it.version, best.value(it.titleId)) < 0 ? QStringLiteral("update-old") : QStringLiteral("update");
    }
}

void LibraryPage::refreshCompat()
{
    for (int idx = 0; idx < items_.size() && idx < rowCells_.size(); ++idx)
        if (rowCells_.at(idx).size() > CFw) styleFw(rowCells_.at(idx).at(CFw), items_.at(idx));
    onSelection();
}

void LibraryPage::applyFilter()
{
    proxy_->setFilters(search_->text(), srcFilter_->currentData().toString(), regFilter_->currentData().toString());
    countLbl_->setText(I18n::instance().t("lib.status", {int(visibleIndexes().size()), int(items_.size())}));
}

// ================================================================= выбор и подробности

const LibraryItem *LibraryPage::currentItem() const
{
    const QModelIndex p = table_->currentIndex();
    if (!p.isValid()) return nullptr;
    const int idx = proxy_->mapToSource(p).siblingAtColumn(0).data(Qt::UserRole + 1).toInt();
    return (idx >= 0 && idx < items_.size()) ? &items_.at(idx) : nullptr;
}

void LibraryPage::onSelection()
{
    const LibraryItem *it = currentItem();
    showOverview(it);
    if (!it) {
        clearDetails(QString());
        return;
    }
    clearDetails(AppPaths::helperAvailable() && !it->gameJson.isEmpty() ? TR("lib.loadingDetails") : TR("lib.helperMissing"));
    // param.json: из описания helper-а, а для дампа в базовом режиме — прямо с диска
    QString raw;
    if (!it->gameJson.isEmpty()) {
        const QString rawJson = QJsonDocument::fromJson(it->gameJson).object().value(QLatin1String("RawParamJson")).toString();
        const QJsonDocument pretty = QJsonDocument::fromJson(rawJson.toUtf8());
        raw = pretty.isNull() ? rawJson : QString::fromUtf8(pretty.toJson(QJsonDocument::Indented));
    } else if (it->isDump) {
        QFile f(it->path + QStringLiteral("/sce_sys/param.json"));
        if (f.open(QIODevice::ReadOnly)) raw = QString::fromUtf8(f.readAll());
    }
    rawView_->setPlainText(raw);
    if (AppPaths::helperAvailable() && !it->gameJson.isEmpty()) detailTimer_.start();
    if (tabs_->currentIndex() == TFiles) loadFiles(*it);
}

void LibraryPage::showOverview(const LibraryItem *it)
{
    if (!it) {
        cover_->clear();
        detail_->clear();
        return;
    }
    QPixmap pm;
    if (it->isDump) pm.load(it->path + QStringLiteral("/sce_sys/icon0.png"));
    if (pm.isNull()) { cover_->setPixmap(QPixmap()); cover_->setText(QStringLiteral("—")); }
    else cover_->setPixmap(pm.scaled(cover_->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));

    auto &i = I18n::instance();
    const FwVerdict verdict = fwVerdict(*it);
    QString fwLine;
    if (!verdict.message.isEmpty()) {
        const QString color = verdict.color.isEmpty() ? QStringLiteral("#3ecf8e") : verdict.color;
        fwLine = QStringLiteral("<span style='color:%1'>").arg(color) + verdict.message.toHtmlEscaped() + QStringLiteral("</span>");
    }
    auto row = [&](const char *k, const QString &v) {
        return QStringLiteral("<tr><td style='color:#98a2b3;padding-right:14px'>%1</td><td>%2</td></tr>")
            .arg(i.t(QString::fromLatin1(k)).toHtmlEscaped(), v.toHtmlEscaped());
    };
    QString html = QStringLiteral("<div style='font-size:13pt;font-weight:600'>%1</div><table>").arg(it->title.toHtmlEscaped());
    html += row("lib.col.titleId", it->titleId) + row("lib.col.contentId", it->contentId) + row("lib.col.role", roleText(it->role))
          + row("lib.col.platform", it->platform) + row("lib.col.region", regionText(it->region)) + row("lib.col.source", sourceText(*it))
          + row("lib.col.size", fmtSize(it->size)) + row("lib.col.version", it->version)
          + row("lib.col.fw", it->requiredFw.isEmpty() ? QStringLiteral("—") : it->requiredFw)
          + row("lib.col.sdk", it->sdk.isEmpty() ? QStringLiteral("—") : it->sdk)
          + row("lib.fw.effective", verdict.effText.isEmpty() ? QStringLiteral("—") : verdict.effText)
          + row("lib.col.path", it->path);
    html += QStringLiteral("</table>");

    // Названия по языкам из param.json (localizedParameters): консоль показывает название на языке системы,
    // а если такого языка в файле нет — берёт defaultLanguage.
    {
        QString rawParam;
        if (!it->gameJson.isEmpty())
            rawParam = QJsonDocument::fromJson(it->gameJson).object().value(QLatin1String("RawParamJson")).toString();
        else if (it->isDump) {
            QFile pf(it->path + QStringLiteral("/sce_sys/param.json"));
            if (pf.open(QIODevice::ReadOnly)) rawParam = QString::fromUtf8(pf.readAll());
        }
        const QJsonObject loc = QJsonDocument::fromJson(rawParam.toUtf8()).object().value(QLatin1String("localizedParameters")).toObject();
        const QString defLang = loc.value(QLatin1String("defaultLanguage")).toString();
        QStringList names;
        for (auto lit = loc.begin(); lit != loc.end(); ++lit) {
            if (!lit.value().isObject()) continue;
            const QString name = lit.value().toObject().value(QLatin1String("titleName")).toString();
            if (name.isEmpty()) continue;
            names << QStringLiteral("<b>%1</b>%2: %3").arg(lit.key().toHtmlEscaped(),
                        lit.key() == defLang ? QStringLiteral(" ★") : QString(), name.toHtmlEscaped());
        }
        if (!names.isEmpty())
            html += QStringLiteral("<p style='color:#98a2b3'>%1<br>%2</p>").arg(TR("lib.titles").toHtmlEscaped(), names.join(QStringLiteral("<br>")));
    }
    if (!fwLine.isEmpty()) html += QStringLiteral("<p>") + fwLine + QStringLiteral("</p>");
    if (it->isDump) {                  // проверки папки игры: эмулятор AMPR и служебные файлы пакета
        const GameCheck::AmprStatus ampr = GameCheck::inspectAmpr(it->path);
        // Строка про AMPR показывается всегда, чтобы было видно, что проверка выполнена и что она нашла.
        const QString fakelib = ampr.fakelib.isEmpty() ? QStringLiteral("—") : ampr.fakelib.join(QStringLiteral(", "));
        if (!ampr.ebootFound) {
            html += QStringLiteral("<p style='color:#98a2b3'>%1</p>").arg(TR("lib.ampr.noEboot").toHtmlEscaped());
        } else if (ampr.importsAmpr) {
            const bool ok = ampr.modulePresent;
            html += QStringLiteral("<p style='color:%1'>%2</p>").arg(ok ? QStringLiteral("#3ecf8e") : QStringLiteral("#ff9f43"),
                        (ok ? i.t("lib.ampr.ok", {ampr.presentVersion}) : TR("lib.ampr.missing")).toHtmlEscaped());
        } else {
            html += QStringLiteral("<p style='color:#98a2b3'>%1</p>").arg(
                        i.t("lib.ampr.no", {ampr.ebootKind, fmtSize(ampr.ebootSize), fakelib}).toHtmlEscaped());
        }
        const QStringList leftovers = GameCheck::findPackageOnly(it->path);
        if (!leftovers.isEmpty())
            html += QStringLiteral("<p style='color:#ff9f43'>%1</p>").arg(i.t("lib.pkgOnly", {leftovers.mid(0, 6).join(QStringLiteral(", ")) + (leftovers.size() > 6 ? QStringLiteral(", …") : QString())}).toHtmlEscaped());
    }
    if (it->sourceDetail.contains(QLatin1String("Retail"), Qt::CaseInsensitive))
        html += QStringLiteral("<p style='color:#ff9f43'>") + TR("hint.retail").toHtmlEscaped() + QStringLiteral("</p>");
    detail_->setText(html);
}

void LibraryPage::clearDetails(const QString &message)
{
    detailStatus_->setText(message);
    for (QLabel *l : art_) { l->setPixmap(QPixmap()); l->clear(); }
    trophyInfo_->clear();
    trophyTable_->setRowCount(0);
    udsInfo_->clear();
    for (QTableWidget *t : {evTable_, statTable_, enumTable_, ruleTable_}) t->setRowCount(0);
    exeView_->clear();
    detailTimer_.stop();
}

QString LibraryPage::tempBaseFor(const QString &path) const
{
    return tempBase() + QLatin1Char('/') + QString::fromLatin1(QCryptographicHash::hash(path.toUtf8(), QCryptographicHash::Sha1).toHex().left(12));
}

void LibraryPage::loadDetails(const LibraryItem &it)
{
    if (!AppPaths::helperAvailable() || it.gameJson.isEmpty()) return;
    const QString base = tempBaseFor(it.path);
    const QString outDir = base + QStringLiteral("/d");
    if (detailCache_.contains(it.path)) {
        QFile f(outDir + QStringLiteral("/details.json"));
        if (f.open(QIODevice::ReadOnly)) {
            applyDetails(QJsonDocument::fromJson(f.readAll()).object(), outDir);
            return;
        }
    }
    QDir().mkpath(outDir);
    const QString gameFile = base + QStringLiteral("/game.json");
    QFile g(gameFile);
    if (!g.open(QIODevice::WriteOnly)) return;
    g.write(it.gameJson);
    g.close();

    if (detailRunner_) { detailRunner_->cancel(); detailRunner_->deleteLater(); }
    auto *r = new ToolRunner(this);
    detailRunner_ = r;
    const QString path = it.path;
    connect(r, &ToolRunner::finished, this, [this, r, outDir, path](bool ok, int, const QString &err) {
        r->deleteLater();
        if (r != detailRunner_) return;            // выбрали другой элемент — результат устарел
        detailRunner_ = nullptr;
        if (!ok) { detailStatus_->setText(I18n::instance().t("lib.detailsFailed", {err})); return; }
        QFile f(outDir + QStringLiteral("/details.json"));
        if (!f.open(QIODevice::ReadOnly)) { detailStatus_->setText(TR("lib.noData")); return; }
        detailCache_.insert(path, outDir);
        const LibraryItem *cur = currentItem();
        if (cur && cur->path == path) applyDetails(QJsonDocument::fromJson(f.readAll()).object(), outDir);
    });
    r->start(AppPaths::helperExe(), {QStringLiteral("details"), QStringLiteral("--game-file"), gameFile, QStringLiteral("--out"), outDir});
}

// Статус раздела: ищем среди sections ключ, содержащий подстроку (имена задаёт PS5 PKG Tool).
static QString sectionNote(const QJsonObject &d, const QString &needle)
{
    const QJsonObject sections = d.value(QLatin1String("sections")).toObject();
    for (auto it = sections.begin(); it != sections.end(); ++it)
        if (it.key().contains(needle, Qt::CaseInsensitive)) {
            const QJsonObject s = it.value().toObject();
            QString t = s.value(QLatin1String("state")).toString();
            const QString m = s.value(QLatin1String("message")).toString();
            if (!m.isEmpty()) t += QStringLiteral(" — ") + m;
            return t;
        }
    return QString();
}

void LibraryPage::applyDetails(const QJsonObject &d, const QString &dir)
{
    auto &i = I18n::instance();
    detailStatus_->clear();
    const QStringList errors = [&] { QStringList e; for (const QJsonValue &v : d.value(QLatin1String("errors")).toArray()) e << v.toString(); return e; }();
    if (!errors.isEmpty()) detailStatus_->setText(i.t("lib.detailsErrors", {errors.join(QStringLiteral("; "))}));

    // обложки
    const QJsonObject art = d.value(QLatin1String("artwork")).toObject();
    static const char *names[4] = {"icon", "pic0", "pic1", "pic2"};
    for (int k = 0; k < 4; ++k) {
        const QString file = art.value(QLatin1String(names[k])).toString();
        QPixmap pm;
        if (!file.isEmpty()) pm.load(dir + QLatin1Char('/') + file);
        if (pm.isNull()) { art_[k]->setPixmap(QPixmap()); art_[k]->setText(QStringLiteral("—")); }
        else art_[k]->setPixmap(pm.scaled(k == 0 ? QSize(150, 150) : QSize(300, 150), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    }

    // трофеи
    const QJsonObject tr = d.value(QLatin1String("trophies")).toObject();
    if (tr.isEmpty()) {
        trophyInfo_->setText(TR("lib.noData") + (sectionNote(d, QStringLiteral("troph")).isEmpty() ? QString() : QStringLiteral(" (") + sectionNote(d, QStringLiteral("troph")) + QStringLiteral(")")));
        trophyTable_->setRowCount(0);
    } else {
        const QJsonArray items = tr.value(QLatin1String("items")).toArray();
        QHash<QString, int> grades;
        trophyTable_->setUpdatesEnabled(false);
        trophyTable_->clear();
        trophyTable_->setColumnCount(6);
        trophyTable_->setHorizontalHeaderLabels({QString(), TR("lib.trophy.col.id"), TR("lib.trophy.col.grade"), TR("lib.trophy.col.name"),
                                                 TR("lib.trophy.col.desc"), TR("lib.trophy.col.hidden")});
        trophyTable_->setRowCount(items.size());
        for (int r = 0; r < items.size(); ++r) {
            const QJsonObject t = items.at(r).toObject();
            grades[t.value(QLatin1String("grade")).toString()]++;
            auto *icon = new QTableWidgetItem;
            const QString f = t.value(QLatin1String("icon")).toString();
            if (!f.isEmpty()) icon->setIcon(QIcon(dir + QLatin1Char('/') + f));
            trophyTable_->setItem(r, 0, icon);
            trophyTable_->setItem(r, 1, new QTableWidgetItem(QString::number(t.value(QLatin1String("id")).toInt())));
            trophyTable_->setItem(r, 2, new QTableWidgetItem(t.value(QLatin1String("grade")).toString()));
            trophyTable_->setItem(r, 3, new QTableWidgetItem(t.value(QLatin1String("name")).toString()));
            auto *desc = new QTableWidgetItem(t.value(QLatin1String("description")).toString());
            desc->setToolTip(desc->text());
            trophyTable_->setItem(r, 4, desc);
            trophyTable_->setItem(r, 5, new QTableWidgetItem(t.value(QLatin1String("hidden")).toBool() ? TR("lib.yes") : QString()));
            trophyTable_->setRowHeight(r, 40);
        }
        trophyTable_->setColumnWidth(0, 44);
        trophyTable_->setColumnWidth(3, 260);
        trophyTable_->setColumnWidth(4, 420);
        trophyTable_->setUpdatesEnabled(true);
        QStringList gl;
        for (auto it = grades.begin(); it != grades.end(); ++it) gl << QStringLiteral("%1: %2").arg(it.key()).arg(it.value());
        gl.sort();
        trophyInfo_->setText(i.t("lib.trophy.summary", {tr.value(QLatin1String("title")).toString(), tr.value(QLatin1String("npCommunicationId")).toString(),
                                                         int(items.size()), tr.value(QLatin1String("language")).toString(),
                                                         tr.value(QLatin1String("integrityValid")).toBool() ? TR("lib.integrity.ok") : TR("lib.integrity.bad"),
                                                         gl.join(QStringLiteral(", "))}));
    }

    // Activities / UDS
    const QJsonObject uds = d.value(QLatin1String("uds")).toObject();
    if (uds.isEmpty()) {
        udsInfo_->setText(TR("lib.noData") + (sectionNote(d, QStringLiteral("activ")).isEmpty() ? QString() : QStringLiteral(" (") + sectionNote(d, QStringLiteral("activ")) + QStringLiteral(")")));
        for (QTableWidget *t : {evTable_, statTable_, enumTable_, ruleTable_}) t->setRowCount(0);
    } else {
        udsInfo_->setText(i.t("lib.uds.summary", {uds.value(QLatin1String("npCommunicationId")).toString(), uds.value(QLatin1String("eventCount")).toInt(),
                                                   uds.value(QLatin1String("statCount")).toInt(), uds.value(QLatin1String("enumGroupCount")).toInt(),
                                                   uds.value(QLatin1String("ruleCount")).toInt()}));
        QList<QStringList> rows;
        for (const QJsonValue &v : uds.value(QLatin1String("events")).toArray()) {
            const QJsonObject o = v.toObject();
            rows << QStringList{o.value(QLatin1String("name")).toString(), o.value(QLatin1String("type")).toString(),
                                o.value(QLatin1String("group")).toString(), QString::number(o.value(QLatin1String("propertyCount")).toInt())};
        }
        fillTable(evTable_, {TR("lib.uds.col.name"), TR("lib.uds.col.type"), TR("lib.uds.col.group"), TR("lib.uds.col.props")}, rows);
        rows.clear();
        for (const QJsonValue &v : uds.value(QLatin1String("stats")).toArray()) {
            const QJsonObject o = v.toObject();
            rows << QStringList{QString::number(o.value(QLatin1String("id")).toInt()), o.value(QLatin1String("name")).toString(), o.value(QLatin1String("group")).toString(),
                                o.value(QLatin1String("origin")).toString(), o.value(QLatin1String("dataType")).toString(), o.value(QLatin1String("aggregation")).toString(),
                                o.value(QLatin1String("min")).toString(), o.value(QLatin1String("max")).toString(), o.value(QLatin1String("initial")).toString()};
        }
        fillTable(statTable_, {TR("lib.uds.col.id"), TR("lib.uds.col.name"), TR("lib.uds.col.group"), TR("lib.uds.col.origin"), TR("lib.uds.col.type"),
                               TR("lib.uds.col.agg"), TR("lib.uds.col.min"), TR("lib.uds.col.max"), TR("lib.uds.col.initial")}, rows);
        rows.clear();
        for (const QJsonValue &v : uds.value(QLatin1String("enums")).toArray()) {
            const QJsonObject o = v.toObject();
            QStringList vals;
            for (const QJsonValue &x : o.value(QLatin1String("values")).toArray()) vals << x.toString();
            rows << QStringList{QString::number(o.value(QLatin1String("id")).toInt()), o.value(QLatin1String("group")).toString(), vals.join(QStringLiteral(", "))};
        }
        fillTable(enumTable_, {TR("lib.uds.col.id"), TR("lib.uds.col.group"), TR("lib.uds.col.values")}, rows);
        rows.clear();
        for (const QJsonValue &v : uds.value(QLatin1String("rules")).toArray()) {
            const QJsonObject o = v.toObject();
            rows << QStringList{QString::number(o.value(QLatin1String("id")).toInt()), o.value(QLatin1String("group")).toString(), o.value(QLatin1String("eventName")).toString(),
                                o.value(QLatin1String("condition")).toString(), o.value(QLatin1String("input")).toString(), o.value(QLatin1String("convert")).toString(),
                                o.value(QLatin1String("outputStat")).toString()};
        }
        fillTable(ruleTable_, {TR("lib.uds.col.id"), TR("lib.uds.col.group"), TR("lib.uds.col.event"), TR("lib.uds.col.cond"), TR("lib.uds.col.input"),
                               TR("lib.uds.col.convert"), TR("lib.uds.col.outstat")}, rows);
    }

    // исполняемый файл
    const QJsonObject exe = d.value(QLatin1String("executable")).toObject();
    if (exe.isEmpty()) {
        exeView_->setPlainText(TR("lib.noData"));
    } else {
        QStringList t;
        auto yn = [](bool b) { return b ? TR("lib.yes") : TR("lib.no"); };
        t << QStringLiteral("%1: %2 (%3)").arg(TR("lib.exe.isSelf"), yn(exe.value(QLatin1String("isSelf")).toBool()), exe.value(QLatin1String("selfMagic")).toString());
        t << QStringLiteral("%1: %2").arg(TR("lib.col.size"), fmtSize(qint64(exe.value(QLatin1String("fileSize")).toDouble())));
        t << QStringLiteral("%1: %2").arg(TR("lib.exe.entry"), exe.value(QLatin1String("entryPoint")).toString());
        t << QStringLiteral("%1: %2").arg(TR("lib.exe.phdr"), QString::number(exe.value(QLatin1String("programHeaders")).toInt()));
        t << QStringLiteral("%1: %2").arg(TR("lib.exe.shdr"), QString::number(exe.value(QLatin1String("sectionHeaders")).toInt()));
        t << QStringLiteral("%1: %2").arg(TR("lib.exe.segments"), QString::number(exe.value(QLatin1String("selfSegments")).toInt()));
        t << QString() << TR("lib.exe.modules") + QLatin1Char(':');
        for (const QJsonValue &v : exe.value(QLatin1String("modules")).toArray()) {
            const QJsonObject m = v.toObject();
            t << QStringLiteral("  %1  [%2]  %3  %4").arg(m.value(QLatin1String("name")).toString(), m.value(QLatin1String("kind")).toString(),
                                                         fmtSize(qint64(m.value(QLatin1String("size")).toDouble())), m.value(QLatin1String("path")).toString());
        }
        exeView_->setPlainText(t.join(QLatin1Char('\n')));
    }
}

// ================================================================= файлы внутри образа

void LibraryPage::loadFiles(const LibraryItem &it)
{
    if (loadedFilesFor_ == it.path) return;
    if (!AppPaths::helperAvailable() || it.gameJson.isEmpty()) {
        filesInfo_->setText(TR("lib.helperMissing"));
        return;
    }
    const QString base = tempBaseFor(it.path);
    QDir().mkpath(base);
    const QString gameFile = base + QStringLiteral("/game.json");
    QFile g(gameFile);
    if (!g.open(QIODevice::WriteOnly)) return;
    g.write(it.gameJson);
    g.close();

    const QString out = base + QStringLiteral("/files.json");
    if (QFileInfo::exists(out)) {
        loadedFilesFor_ = it.path;
        applyFiles(out);
        return;
    }
    filesInfo_->setText(TR("lib.files.loading"));
    if (filesRunner_) { filesRunner_->cancel(); filesRunner_->deleteLater(); }
    auto *r = new ToolRunner(this);
    filesRunner_ = r;
    const QString path = it.path;
    connect(r, &ToolRunner::finished, this, [this, r, out, path](bool ok, int, const QString &err) {
        r->deleteLater();
        if (r != filesRunner_) return;
        filesRunner_ = nullptr;
        if (!ok) { filesInfo_->setText(I18n::instance().t("lib.detailsFailed", {err})); return; }
        const LibraryItem *cur = currentItem();
        if (!cur || cur->path != path) return;
        loadedFilesFor_ = path;
        applyFiles(out);
    });
    r->start(AppPaths::helperExe(), {QStringLiteral("files"), QStringLiteral("--game-file"), gameFile, QStringLiteral("--out"), out});
}

void LibraryPage::applyFiles(const QString &jsonPath)
{
    QFile f(jsonPath);
    if (!f.open(QIODevice::ReadOnly)) return;
    const QJsonArray arr = QJsonDocument::fromJson(f.readAll()).array();
    constexpr int kMaxRows = 200000;
    const int n = qMin(int(arr.size()), kMaxRows);

    filesProxy_->setSourceModel(nullptr);          // пока заполняем — прокси отключён (иначе на 100 000 строк очень медленно)
    filesModel_->removeRows(0, filesModel_->rowCount());
    filesModel_->setColumnCount(3);
    qint64 total = 0;
    for (int k = 0; k < n; ++k) {
        const QJsonObject o = arr.at(k).toObject();
        const qint64 size = qint64(o.value(QLatin1String("s")).toDouble());
        total += size;
        auto *p = new QStandardItem(o.value(QLatin1String("p")).toString());
        auto *s = new QStandardItem(fmtSize(size));
        auto *g = new QStandardItem(o.value(QLatin1String("o")).toString());
        p->setData(p->text().toLower(), Qt::UserRole);
        s->setData(QVariant::fromValue<qint64>(size), Qt::UserRole);
        g->setData(g->text().toLower(), Qt::UserRole);
        for (QStandardItem *c : {p, s, g}) c->setEditable(false);
        filesModel_->appendRow({p, s, g});
    }
    filesModel_->setHorizontalHeaderLabels({TR("lib.files.col.path"), TR("lib.files.col.size"), TR("lib.files.col.origin")});
    filesProxy_->setSourceModel(filesModel_);
    filesView_->setColumnWidth(0, 560);
    filesView_->setColumnWidth(1, 110);
    QString info = I18n::instance().t("lib.files.count", {int(arr.size()), fmtSize(total)});
    if (arr.size() > kMaxRows) info += QStringLiteral(" · ") + I18n::instance().t("lib.files.truncated", {kMaxRows});
    filesInfo_->setText(info + QStringLiteral(" · ") + TR("lib.files.hint"));
}

void LibraryPage::extractSelectedFiles()
{
    const QModelIndexList rows = filesView_->selectionModel()->selectedRows(0);
    if (rows.isEmpty()) {
        filesInfo_->setText(TR("lib.files.selectFirst"));
        return;
    }
    const LibraryItem *item = nullptr;
    for (const LibraryItem &it : std::as_const(items_))
        if (it.path == loadedFilesFor_) { item = &it; break; }
    if (!item || item->gameJson.isEmpty()) return;

    const QString dir = QFileDialog::getExistingDirectory(this, TR("lib.files.pickDir"));
    if (dir.isEmpty()) return;

    // Файлы задания лежат в постоянной папке: прерванную задачу можно повторить после перезапуска программы.
    const QString jobDir = AppPaths::dataDir() + QStringLiteral("/jobs/") + QString::number(QDateTime::currentMSecsSinceEpoch());
    QDir().mkpath(jobDir);
    QFile g(jobDir + QStringLiteral("/game.json"));
    QFile l(jobDir + QStringLiteral("/paths.txt"));
    if (!g.open(QIODevice::WriteOnly) || !l.open(QIODevice::WriteOnly)) return;
    g.write(item->gameJson);
    for (const QModelIndex &idx : rows) l.write(idx.data(Qt::DisplayRole).toString().toUtf8() + '\n');
    g.close();
    l.close();

    emit runRequested({JobSpec{TR("lib.files.job"), AppPaths::helperExe(),
                               {QStringLiteral("extract"), QStringLiteral("--game-file"), jobDir + QStringLiteral("/game.json"),
                                QStringLiteral("--paths-file"), jobDir + QStringLiteral("/paths.txt"), QStringLiteral("--out"), dir},
                               QString(), 1}},
                      dir);
}

// ================================================================= папки и меню

void LibraryPage::addFolder()
{
    const QString d = QFileDialog::getExistingDirectory(this, TR("lib.add"));
    if (d.isEmpty()) return;
    QStringList f = folders();
    const QString nd = QDir::toNativeSeparators(d);
    if (!f.contains(nd)) f << nd;
    AppPaths::settings().setValue("library/folders", f);
    startScan();
}

void LibraryPage::removeFolder()
{
    QStringList f = folders();
    if (f.isEmpty()) return;
    bool ok = false;
    const QString sel = QInputDialog::getItem(this, TR("lib.remove"), TR("lib.removePrompt"), f, 0, false, &ok);
    if (!ok) return;
    f.removeAll(sel);
    AppPaths::settings().setValue("library/folders", f);
    startScan();
}

void LibraryPage::showMenu(const QPoint &pos)
{
    const QModelIndex idx = table_->indexAt(pos);
    if (!idx.isValid()) return;
    table_->setCurrentIndex(idx);
    const LibraryItem *it = currentItem();
    if (!it) return;
    const LibraryItem item = *it;

    QMenu m(this);
    m.addAction(TR("lib.ctx.open"), this, [item] {
        QDesktopServices::openUrl(QUrl::fromLocalFile(item.isDump ? item.path : QFileInfo(item.path).absolutePath()));
    });
    m.addSeparator();
    m.addAction(TR("lib.ctx.copyPath"), this, [item] { QApplication::clipboard()->setText(item.path); });
    m.addAction(TR("lib.ctx.copyTitleId"), this, [item] { QApplication::clipboard()->setText(item.titleId); });
    m.addAction(TR("lib.ctx.copyContentId"), this, [item] { QApplication::clipboard()->setText(item.contentId); });
    m.addSeparator();
    if (item.isDump) m.addAction(TR("lib.ctx.toPack"), this, [this, item] { emit openIn(0, item.path); });
    if (item.isDump || item.source != QLatin1String("pkg"))
        m.addAction(TR("lib.ctx.toFpkg"), this, [this, item] { emit openIn(1, item.path); });
    if (!item.isDump) m.addAction(TR("lib.ctx.toTools"), this, [this, item] { emit openIn(2, item.path); });
    if (item.isDump) m.addAction(TR("lib.ctx.toBackport"), this, [this, item] { emit openIn(3, item.path); });
    m.exec(table_->viewport()->mapToGlobal(pos));
}


// ================================================================= дубликаты, переименование, экспорт

void LibraryPage::findDuplicates()
{
    if (items_.isEmpty()) return;
    DuplicatesDialog dlg(items_, this);
    dlg.exec();
    if (dlg.changed()) startScan();
}

// Переименование касается видимых (отфильтрованных) элементов: отфильтруйте нужные и откройте диалог.
void LibraryPage::renameItems()
{
    QList<LibraryItem> list;
    for (int idx : visibleIndexes()) list << items_.at(idx);
    if (list.isEmpty()) return;
    RenameDialog dlg(list, this);
    dlg.exec();
    if (dlg.changed()) startScan();
}

static QString csvCell(const QString &v)
{
    QString s = v;
    s.replace(QLatin1Char('"'), QStringLiteral("\"\""));
    return QLatin1Char('"') + s + QLatin1Char('"');
}

// Экспорт видимых строк в CSV (разделитель «;», UTF-8 с BOM — Excel открывает без «кракозябр») или JSON.
void LibraryPage::exportList()
{
    const QList<int> idxs = visibleIndexes();
    if (idxs.isEmpty()) return;
    QString selected;
    const QString path = QFileDialog::getSaveFileName(this, TR("lib.export"), QStringLiteral("PS5_Library.csv"),
                                                      TR("lib.export.filter"), &selected);
    if (path.isEmpty()) return;
    const bool json = path.endsWith(QStringLiteral(".json"), Qt::CaseInsensitive);

    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) { QMessageBox::warning(this, TR("app.name"), TR("lib.export.failed")); return; }
    if (json) {
        QJsonArray arr;
        for (int idx : idxs) {
            const LibraryItem &it = items_.at(idx);
            QJsonObject o;
            o["title"] = it.title; o["titleId"] = it.titleId; o["contentId"] = it.contentId; o["role"] = it.role;
            o["platform"] = it.platform; o["region"] = it.region; o["source"] = it.sourceDetail.isEmpty() ? it.source : it.sourceDetail;
            o["sizeBytes"] = double(it.size); o["version"] = it.version; o["requiredFw"] = it.requiredFw; o["sdk"] = it.sdk;
            o["file"] = it.fileName; o["path"] = it.path;
            arr.append(o);
        }
        f.write(QJsonDocument(arr).toJson(QJsonDocument::Indented));
    } else {
        QString text;
        text += QStringLiteral("Title;TitleID;ContentID;Role;Platform;Region;Source;SizeBytes;Version;RequiredFW;SDK;File;Path\r\n");
        for (int idx : idxs) {
            const LibraryItem &it = items_.at(idx);
            text += QStringList{csvCell(it.title), csvCell(it.titleId), csvCell(it.contentId), csvCell(it.role), csvCell(it.platform),
                                csvCell(it.region), csvCell(it.sourceDetail.isEmpty() ? it.source : it.sourceDetail),
                                QString::number(it.size), csvCell(it.version), csvCell(it.requiredFw), csvCell(it.sdk),
                                csvCell(it.fileName), csvCell(it.path)}.join(QLatin1Char(';')) + QStringLiteral("\r\n");
        }
        f.write("\xEF\xBB\xBF");
        f.write(text.toUtf8());
    }
    if (!f.commit()) { QMessageBox::warning(this, TR("app.name"), TR("lib.export.failed")); return; }
    status_->setText(I18n::instance().t("lib.export.done", {int(idxs.size()), QDir::toNativeSeparators(path)}));
}

// Сверка двух элементов библиотеки (наш образ и заведомо рабочий). Текущая строка предлагается как «A».
void LibraryPage::compareImages()
{
    if (items_.size() < 2) {
        QMessageBox::information(this, TR("app.name"), TR("cmp.needTwo"));
        return;
    }
    const LibraryItem *cur = currentItem();
    const int pre = cur ? int(cur - items_.constData()) : 0;
    CompareDialog dlg(items_, pre, this);
    dlg.exec();
}
