#include "CompareDialog.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMessageBox>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QVBoxLayout>

#include "AppPaths.h"
#include "I18n.h"
#include "GameCheck.h"
#include "ToolRunner.h"

static QTableWidget *makeCmpTable(const QStringList &headers)
{
    auto *t = new QTableWidget(0, headers.size());
    t->setHorizontalHeaderLabels(headers);
    t->setEditTriggers(QAbstractItemView::NoEditTriggers);
    t->setSelectionBehavior(QAbstractItemView::SelectRows);
    t->setShowGrid(false);
    t->setSortingEnabled(true);
    t->verticalHeader()->hide();
    t->horizontalHeader()->setStretchLastSection(true);
    return t;
}

static bool isExecutable(const QString &path)
{
    const QString l = path.toLower();
    return l.endsWith(QLatin1String("eboot.bin")) || l.endsWith(QLatin1String(".sprx")) || l.endsWith(QLatin1String(".self"))
           || l.endsWith(QLatin1String(".elf")) || l.endsWith(QLatin1String(".prx")) || l.endsWith(QLatin1String(".dll"));
}

CompareDialog::CompareDialog(const QList<LibraryItem> &items, int preselectA, QWidget *parent) : QDialog(parent), items_(items)
{
    setWindowTitle(TR("cmp.title"));
    resize(1100, 700);
    workDir_ = QStandardPaths::writableLocation(QStandardPaths::TempLocation) + QStringLiteral("/PS5CombineAIO/cmp");
    QDir().mkpath(workDir_);

    auto *v = new QVBoxLayout(this);
    auto *hint = new QLabel(TR("cmp.hint"));
    hint->setWordWrap(true);
    hint->setObjectName("muted");
    v->addWidget(hint);

    a_ = new QComboBox;
    b_ = new QComboBox;
    for (const LibraryItem &it : std::as_const(items_)) {
        a_->addItem(label(it));
        b_->addItem(label(it));
    }
    if (preselectA >= 0 && preselectA < items_.size()) a_->setCurrentIndex(preselectA);
    b_->setCurrentIndex(items_.size() > 1 ? (a_->currentIndex() == 0 ? 1 : 0) : 0);
    auto *row1 = new QHBoxLayout;
    row1->addWidget(new QLabel(TR("cmp.a")));
    row1->addWidget(a_, 1);
    auto *row2 = new QHBoxLayout;
    row2->addWidget(new QLabel(TR("cmp.b")));
    row2->addWidget(b_, 1);
    v->addLayout(row1);
    v->addLayout(row2);

    deep_ = new QCheckBox(TR("cmp.deep"));
    deep_->setToolTip(TR("cmp.deepTip"));
    v->addWidget(deep_);

    auto *row3 = new QHBoxLayout;
    bStart_ = new QPushButton(TR("cmp.start"));
    bStart_->setObjectName("primary");
    bCancel_ = new QPushButton(TR("job.cancel"));
    bCancel_->setEnabled(false);
    bSave_ = new QPushButton(TR("cmp.save"));
    bSave_->setEnabled(false);
    auto *bClose = new QPushButton(TR("dup.close"));
    row3->addWidget(bStart_);
    row3->addWidget(bCancel_);
    row3->addWidget(bSave_);
    row3->addStretch(1);
    row3->addWidget(bClose);
    v->addLayout(row3);

    bar_ = new QProgressBar;
    bar_->setRange(0, 100);
    status_ = new QLabel;
    status_->setObjectName("muted");
    status_->setWordWrap(true);
    v->addWidget(bar_);
    v->addWidget(status_);

    tabs_ = new QTabWidget;
    summary_ = new QPlainTextEdit;
    summary_->setReadOnly(true);
    onlyA_ = makeCmpTable({TR("cmp.col.path"), TR("lib.col.size")});
    onlyB_ = makeCmpTable({TR("cmp.col.path"), TR("lib.col.size")});
    differ_ = makeCmpTable({TR("cmp.col.path"), TR("cmp.col.sizeA"), TR("cmp.col.sizeB"), TR("cmp.col.why")});
    params_ = makeCmpTable({TR("cmp.col.key"), TR("cmp.col.valA"), TR("cmp.col.valB")});
    geometry_ = makeCmpTable({TR("cmp.col.key"), TR("cmp.col.valA"), TR("cmp.col.valB")});
    tabs_->addTab(summary_, TR("cmp.tab.summary"));
    tabs_->addTab(onlyB_, TR("cmp.tab.onlyB"));
    tabs_->addTab(onlyA_, TR("cmp.tab.onlyA"));
    tabs_->addTab(differ_, TR("cmp.tab.differ"));
    tabs_->addTab(params_, TR("cmp.tab.params"));
    tabs_->addTab(geometry_, TR("cmp.tab.geometry"));
    v->addWidget(tabs_, 1);

    connect(bStart_, &QPushButton::clicked, this, &CompareDialog::start);
    connect(bCancel_, &QPushButton::clicked, this, &CompareDialog::cancel);
    connect(bSave_, &QPushButton::clicked, this, &CompareDialog::saveReport);
    connect(bClose, &QPushButton::clicked, this, &QDialog::accept);
}

CompareDialog::~CompareDialog()
{
    if (runner_) runner_->stopAndWait(5000);
}

QString CompareDialog::fmtBytes(qint64 b) const
{
    auto &i = I18n::instance();
    const double v = double(b);
    if (b >= (1LL << 30)) return QString::number(v / double(1LL << 30), 'f', 2) + QLatin1Char(' ') + i.t("unit.gb");
    if (b >= (1LL << 20)) return QString::number(v / double(1LL << 20), 'f', 1) + QLatin1Char(' ') + i.t("unit.mb");
    if (b >= (1LL << 10)) return QString::number(v / double(1LL << 10), 'f', 0) + QLatin1Char(' ') + i.t("unit.kb");
    return QString::number(b) + QLatin1Char(' ') + i.t("unit.b");
}

QString CompareDialog::label(const LibraryItem &it) const
{
    return QStringLiteral("%1  ·  %2  ·  %3").arg(it.title, it.fileName, it.sourceDetail.isEmpty() ? it.source.toUpper() : it.sourceDetail);
}

void CompareDialog::start()
{
    if (!AppPaths::helperAvailable()) { QMessageBox::information(this, TR("app.name"), TR("tools.needHelper")); return; }
    const int ia = a_->currentIndex(), ib = b_->currentIndex();
    if (ia < 0 || ib < 0 || ia >= items_.size() || ib >= items_.size()) return;
    if (ia == ib) { QMessageBox::warning(this, TR("app.name"), TR("cmp.same")); return; }
    if (items_.at(ia).gameJson.isEmpty() || items_.at(ib).gameJson.isEmpty()) {
        QMessageBox::warning(this, TR("app.name"), TR("lib.helperMissing"));
        return;
    }

    const QString fa = workDir_ + QStringLiteral("/a.json"), fb = workDir_ + QStringLiteral("/b.json");
    reportPath_ = workDir_ + QStringLiteral("/report.json");
    QFile::remove(reportPath_);
    for (const auto &pair : {qMakePair(fa, items_.at(ia).gameJson), qMakePair(fb, items_.at(ib).gameJson)}) {
        QFile f(pair.first);
        if (!f.open(QIODevice::WriteOnly)) return;
        f.write(pair.second);
    }

    bar_->setValue(0);
    status_->setText(TR("cmp.running"));
    bSave_->setEnabled(false);
    bStart_->setEnabled(false);
    bCancel_->setEnabled(true);
    runner_ = new ToolRunner(this);
    connect(runner_, &ToolRunner::progress, bar_, &QProgressBar::setValue);
    connect(runner_, &ToolRunner::logLine, this, [this](const QString &line, bool) {
        static const QRegularExpression prog(QStringLiteral(R"(^\[[^\]]*\]\s*\d)"));
        if (prog.match(line).hasMatch()) status_->setText(line);
    });
    connect(runner_, &ToolRunner::finished, this, [this](bool ok, int, const QString &err) { onFinished(ok, err); });
    QStringList args{QStringLiteral("compare"), QStringLiteral("--game-a"), fa, QStringLiteral("--game-b"), fb,
                     QStringLiteral("--out"), reportPath_};
    if (deep_->isChecked()) args << QStringLiteral("--deep");
    runner_->start(AppPaths::helperExe(), args);
}

void CompareDialog::cancel()
{
    if (runner_) runner_->cancel();
}

void CompareDialog::onFinished(bool ok, const QString &err)
{
    const bool cancelled = runner_ && runner_->wasCancelled();
    runner_->deleteLater();
    runner_ = nullptr;
    bStart_->setEnabled(true);
    bCancel_->setEnabled(false);
    if (cancelled) { status_->setText(TR("job.cancelledShort")); return; }
    if (!ok) { status_->setText(I18n::instance().t("lib.detailsFailed", {err})); return; }
    QFile f(reportPath_);
    if (!f.open(QIODevice::ReadOnly)) { status_->setText(TR("lib.noData")); return; }
    bar_->setValue(100);
    status_->setText(TR("cmp.done"));
    showReport(QJsonDocument::fromJson(f.readAll()).object());
}

static void fillRows(QTableWidget *t, const QList<QStringList> &rows)
{
    t->setSortingEnabled(false);
    t->setRowCount(0);
    t->setRowCount(rows.size());
    for (int r = 0; r < rows.size(); ++r)
        for (int c = 0; c < rows.at(r).size(); ++c) {
            auto *it = new QTableWidgetItem(rows.at(r).at(c));
            it->setToolTip(rows.at(r).at(c));
            t->setItem(r, c, it);
        }
    t->setSortingEnabled(true);
    t->resizeColumnToContents(0);
    if (t->columnWidth(0) > 520) t->setColumnWidth(0, 520);
}

void CompareDialog::showReport(const QJsonObject &r)
{
    auto &i = I18n::instance();
    const QJsonObject A = r.value(QLatin1String("a")).toObject(), B = r.value(QLatin1String("b")).toObject();
    auto side = [&](const QJsonObject &o) {
        return QStringList{
            QStringLiteral("  %1 (%2)").arg(o.value(QLatin1String("title")).toString(), o.value(QLatin1String("titleId")).toString()),
            QStringLiteral("  %1: %2").arg(TR("lib.col.source"), o.value(QLatin1String("format")).toString()),
            QStringLiteral("  %1: %2   %3: %4").arg(TR("lib.col.version"), o.value(QLatin1String("contentVersion")).toString(),
                                                  TR("lib.col.fw"), o.value(QLatin1String("requiredFw")).toString()),
            QStringLiteral("  %1: %2").arg(TR("lib.col.sdk"), o.value(QLatin1String("sdk")).toString().isEmpty() ? QStringLiteral("—") : o.value(QLatin1String("sdk")).toString()),
            QStringLiteral("  %1: %2   %3: %4").arg(TR("cmp.files"), QString::number(o.value(QLatin1String("fileCount")).toInt()),
                                                  TR("lib.col.size"), fmtBytes(qint64(o.value(QLatin1String("totalSize")).toDouble()))),
            QStringLiteral("  eboot.bin: %1").arg(o.value(QLatin1String("eboot")).toString().isEmpty() ? TR("cmp.notFound") : o.value(QLatin1String("eboot")).toString())};
    };

    const QJsonArray onlyA = r.value(QLatin1String("onlyA")).toArray(), onlyB = r.value(QLatin1String("onlyB")).toArray();
    const QJsonArray differ = r.value(QLatin1String("differ")).toArray(), caseDiff = r.value(QLatin1String("caseDiff")).toArray();
    const QJsonArray params = r.value(QLatin1String("paramDiff")).toArray();

    QList<QStringList> rows;
    for (const QJsonValue &v : onlyB) rows << QStringList{v.toObject().value(QLatin1String("p")).toString(), fmtBytes(qint64(v.toObject().value(QLatin1String("s")).toDouble()))};
    fillRows(onlyB_, rows);
    rows.clear();
    for (const QJsonValue &v : onlyA) rows << QStringList{v.toObject().value(QLatin1String("p")).toString(), fmtBytes(qint64(v.toObject().value(QLatin1String("s")).toDouble()))};
    fillRows(onlyA_, rows);
    rows.clear();
    int diffExe = 0, diffData = 0;
    for (const QJsonValue &v : differ) {
        const QJsonObject o = v.toObject();
        const QString path = o.value(QLatin1String("p")).toString();
        const bool bySize = o.value(QLatin1String("why")).toString() == QLatin1String("size");
        (isExecutable(path) ? diffExe : diffData)++;
        rows << QStringList{path, fmtBytes(qint64(o.value(QLatin1String("sa")).toDouble())), fmtBytes(qint64(o.value(QLatin1String("sb")).toDouble())),
                            bySize ? TR("cmp.why.size") : i.t("cmp.why.content", {QString::number(qint64(o.value(QLatin1String("offset")).toDouble()))})};
    }
    fillRows(differ_, rows);
    rows.clear();
    for (const QJsonValue &v : params) {
        const QJsonObject o = v.toObject();
        rows << QStringList{o.value(QLatin1String("key")).toString(), o.value(QLatin1String("a")).isNull() ? TR("cmp.absent") : o.value(QLatin1String("a")).toString(),
                            o.value(QLatin1String("b")).isNull() ? TR("cmp.absent") : o.value(QLatin1String("b")).toString()};
    }
    fillRows(params_, rows);
    tabs_->setTabText(1, i.t("cmp.tab.onlyBn", {int(onlyB.size())}));
    tabs_->setTabText(2, i.t("cmp.tab.onlyAn", {int(onlyA.size())}));
    tabs_->setTabText(3, i.t("cmp.tab.differN", {int(differ.size())}));
    tabs_->setTabText(4, i.t("cmp.tab.paramsN", {int(params.size())}));

    // Структура самого exFAT (только если оба — «сырые» образы .exfat)
    const QJsonObject gA = A.value(QLatin1String("exfat")).toObject(), gB = B.value(QLatin1String("exfat")).toObject();
    QStringList geoDiffs;
    {
        struct G { const char *key, *labelKey; bool bytes; };
        const G rowsDef[] = {{"clusterSize", "cmp.geo.cluster", true}, {"bytesPerSector", "cmp.geo.sector", true},
                             {"volumeLength", "cmp.geo.volume", true}, {"fileLength", "cmp.geo.file", true},
                             {"fatOffset", "cmp.geo.fatOffset", false}, {"fatLength", "cmp.geo.fatLength", false},
                             {"clusterHeapOffset", "cmp.geo.heapOffset", false}, {"clusterCount", "cmp.geo.clusterCount", false},
                             {"rootCluster", "cmp.geo.root", false}, {"fats", "cmp.geo.fats", false},
                             {"volumeFlags", "cmp.geo.flags", false}, {"percentInUse", "cmp.geo.percent", false},
                             {"revision", "cmp.geo.revision", false}, {"serial", "cmp.geo.serial", false}};
        QList<QStringList> grows;
        auto val = [&](const QJsonObject &o, const G &g) {
            if (o.isEmpty() || !o.contains(QLatin1String(g.key))) return QStringLiteral("—");
            const QJsonValue v = o.value(QLatin1String(g.key));
            if (v.isString()) return v.toString();
            const qint64 n = qint64(v.toDouble());
            return g.bytes ? QStringLiteral("%1 (%2)").arg(fmtBytes(n)).arg(n) : QString::number(n);
        };
        for (const G &g : rowsDef) {
            const QString va = val(gA, g), vb = val(gB, g);
            grows << QStringList{I18n::instance().t(QString::fromLatin1(g.labelKey)), va, vb};
            // серийный номер, размеры, число кластеров, длина FAT и смещение области кластеров считаются из размера образа и сами по себе ничего не значат
            // (а «занято, %» MkPFS и PS5 PKG Tool всегда пишут как 255 = «не определено», так что отличие в нём говорит о другом инструменте)
            const QString k = QString::fromLatin1(g.key);
            if (va != vb && !gA.isEmpty() && !gB.isEmpty() && k != QLatin1String("serial") && k != QLatin1String("fileLength")
                && k != QLatin1String("volumeLength") && k != QLatin1String("percentInUse") && k != QLatin1String("clusterCount")
                && k != QLatin1String("fatLength") && k != QLatin1String("rootCluster") && k != QLatin1String("clusterHeapOffset"))
                geoDiffs << I18n::instance().t(QString::fromLatin1(g.labelKey));
        }
        fillRows(geometry_, grows);
    }

    // ----- текстовая сводка и выводы
    QStringList t;
    t << QStringLiteral("A — ") + TR("cmp.a") << side(A) << QString() << QStringLiteral("B — ") + TR("cmp.b") << side(B) << QString();
    t << i.t("cmp.sum.counts", {int(onlyB.size()), int(onlyA.size()), int(differ.size()), r.value(QLatin1String("sameVerified")).toInt(),
                                r.value(QLatin1String("sameBySizeOnly")).toInt()});
    if (!r.value(QLatin1String("deep")).toBool())
        t << TR("cmp.sum.notDeep");
    t << QString() << TR("cmp.sum.verdict");

    bool anyFinding = false;
    auto finding = [&](const QString &s) { t << QStringLiteral("• ") + s; anyFinding = true; };
    const QString pa = A.value(QLatin1String("rootPrefix")).toString(), pb = B.value(QLatin1String("rootPrefix")).toString();
    if (A.value(QLatin1String("eboot")).toString().isEmpty()) finding(TR("cmp.f.noEbootA"));
    if (B.value(QLatin1String("eboot")).toString().isEmpty()) finding(TR("cmp.f.noEbootB"));
    if (pa != pb) finding(i.t("cmp.f.rootShift", {pa.isEmpty() ? QStringLiteral("/") : pa, pb.isEmpty() ? QStringLiteral("/") : pb}));
    for (const QString &key : {QStringLiteral("requiredA"), QStringLiteral("requiredB")})
        for (const QJsonValue &v : r.value(key).toArray())
            if (!v.toObject().value(QLatin1String("present")).toBool())
                finding(i.t("cmp.f.missingRequired", {key == QLatin1String("requiredA") ? QStringLiteral("A") : QStringLiteral("B"), v.toObject().value(QLatin1String("path")).toString()}));
    // особые случаи: эмулятор AMPR и служебные файлы пакета
    auto hasPath = [](const QJsonArray &arr, const QString &lowerPath) {
        for (const QJsonValue &v : arr)
            if (v.toObject().value(QLatin1String("p")).toString().toLower().replace(QLatin1Char('\\'), QLatin1Char('/')).endsWith(lowerPath)) return true;
        return false;
    };
    if (hasPath(onlyB, QStringLiteral("fakelib/libsceampr.sprx"))) finding(TR("cmp.f.amprMissing"));
    if (hasPath(onlyB, QStringLiteral("ampr_emu.index"))) finding(TR("cmp.f.amprIndex"));
    if (hasPath(differ, QStringLiteral("fakelib/libsceampr.sprx"))) finding(TR("cmp.f.amprDiffer"));
    {
        QStringList ours;
        for (const QJsonValue &v : onlyA) {
            const QString path = v.toObject().value(QLatin1String("p")).toString();
            if (GameCheck::isPackageOnlyPath(path)) ours << path;
        }
        if (!ours.isEmpty()) finding(i.t("cmp.f.pkgOnly", {ours.mid(0, 6).join(QStringLiteral(", ")) + (ours.size() > 6 ? QStringLiteral(", …") : QString())}));
    }
    if (!geoDiffs.isEmpty()) finding(i.t("cmp.f.geometry", {geoDiffs.join(QStringLiteral(", "))}));
    if (!onlyB.isEmpty()) finding(i.t("cmp.f.onlyB", {int(onlyB.size())}));
    if (!onlyA.isEmpty()) finding(i.t("cmp.f.onlyA", {int(onlyA.size())}));
    if (diffData > 0) finding(i.t("cmp.f.diffData", {diffData}));
    if (diffExe > 0) finding(i.t("cmp.f.diffExe", {diffExe}));
    if (!caseDiff.isEmpty()) finding(i.t("cmp.f.case", {int(caseDiff.size())}));
    for (const QJsonValue &v : params) {
        const QString key = v.toObject().value(QLatin1String("key")).toString();
        if (key == QLatin1String("requiredSystemSoftwareVersion") || key == QLatin1String("sdkVersion") || key == QLatin1String("contentVersion")
            || key == QLatin1String("titleId") || key == QLatin1String("contentId") || key == QLatin1String("applicationCategoryType"))
            finding(i.t("cmp.f.param", {key, v.toObject().value(QLatin1String("a")).toString(), v.toObject().value(QLatin1String("b")).toString()}));
    }
    if (!anyFinding) t << TR("cmp.f.identical");
    summary_->setPlainText(t.join(QLatin1Char('\n')));
    tabs_->setCurrentIndex(0);

    // отчёт целиком — для сохранения / отправки
    QStringList full = t;
    auto dump = [&](const QString &name, const QTableWidget *tab) {
        full << QString() << QStringLiteral("=== %1 ===").arg(name);
        for (int rr = 0; rr < tab->rowCount(); ++rr) {
            QStringList cells;
            for (int c = 0; c < tab->columnCount(); ++c) cells << tab->item(rr, c)->text();
            full << cells.join(QStringLiteral("  |  "));
        }
    };
    dump(TR("cmp.tab.onlyB"), onlyB_);
    dump(TR("cmp.tab.onlyA"), onlyA_);
    dump(TR("cmp.tab.differ"), differ_);
    dump(TR("cmp.tab.params"), params_);
    dump(TR("cmp.tab.geometry"), geometry_);
    reportText_ = full.join(QLatin1Char('\n'));
    bSave_->setEnabled(true);
}

void CompareDialog::saveReport()
{
    const QString path = QFileDialog::getSaveFileName(this, TR("cmp.save"), QStringLiteral("compare_report.txt"), TR("cmp.saveFilter"));
    if (path.isEmpty()) return;
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) { QMessageBox::warning(this, TR("app.name"), TR("lib.export.failed")); return; }
    f.write("\xEF\xBB\xBF");
    f.write(reportText_.toUtf8());
}
