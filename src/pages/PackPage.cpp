#include "PackPage.h"

#include <QFile>
#include <QFormLayout>
#include <QInputDialog>
#include <QMessageBox>

#include "AppPaths.h"
#include "FtpUpload.h"
#include "GameCheck.h"
#include "ParamJson.h"

PackPage::PackPage(QWidget *parent) : PageBase(parent)
{
    auto *page = new QVBoxLayout(this);
    page->setContentsMargins(28, 24, 28, 12);
    page->setSpacing(14);
    header(page, "pack.title", "pack.subtitle");

    // --- источник и результат ---
    auto *c1 = card(page, "pack.cardSource");
    auto *form = new QFormLayout;
    form->setHorizontalSpacing(16);
    form->setVerticalSpacing(10);
    mode_ = new QComboBox;
    fillCombo(mode_, {{"folder", "pack.mode.folder"}, {"folder-raw", "pack.mode.raw"},
                      {"file", "pack.mode.file"}, {"exfat", "pack.mode.exfat"}});
    src_ = new PathRow(PathRow::OpenDir);
    dst_ = new PathRow(PathRow::SaveFile);
    form->addRow(R(new QLabel, "pack.mode"), mode_);
    form->addRow(R(new QLabel, "pack.source"), src_);
    form->addRow(R(new QLabel, "pack.output"), dst_);
    c1->addLayout(form);
    hint_ = new QLabel;
    hint_->setObjectName("muted");
    hint_->setWordWrap(true);
    c1->addWidget(hint_);

    // --- параметры ---
    auto *c2 = card(page, "pack.cardOptions");
    auto *f2 = new QFormLayout;
    f2->setHorizontalSpacing(16);
    version_ = new QComboBox;
    fillCombo(version_, {{"PS5", "pack.ver.ps5"}, {"PS4", "pack.ver.ps4"}});
    f2->addRow(R(new QLabel, "pack.profile"), version_);
    level_ = new QSpinBox;
    level_->setRange(0, 9);
    level_->setValue(7);                       // как у mkpfs по умолчанию
    f2->addRow(R(new QLabel, "pack.level"), level_);
    cluster_ = new QComboBox;
    cluster_->addItem(QStringLiteral("auto (64 KiB)"), QStringLiteral("auto"));
    for (const int kib : {16, 32, 64, 128, 256, 512, 1024})
        cluster_->addItem(kib >= 1024 ? QStringLiteral("%1 MiB").arg(kib / 1024) : QStringLiteral("%1 KiB").arg(kib), QString::number(kib * 1024));
    cluster_->setToolTip(TR("pack.clusterTip"));
    f2->addRow(R(new QLabel, "pack.cluster"), cluster_);
    c2->addLayout(f2);
    noCompress_ = R(new QCheckBox, "pack.noCompress");
    caseSens_ = R(new QCheckBox, "pack.caseSensitive");
    verify_ = R(new QCheckBox, "pack.verify");
    verbose_ = R(new QCheckBox, "pack.verbose");
    verify_->setChecked(true);
    c2->addWidget(noCompress_);
    c2->addWidget(caseSens_);
    c2->addWidget(verify_);
    c2->addWidget(verbose_);
    ftpUpload_ = R(new QCheckBox, "pack.ftpUpload");
    ftpUpload_->setChecked(FtpUpload::isReady());
    ftpUpload_->setEnabled(FtpUpload::isReady());
    ftpUpload_->setToolTip(TR("pack.ftpUploadTip"));
    c2->addWidget(ftpUpload_);

    run_ = R(new QPushButton, "pack.run");
    run_->setObjectName("primary");
    auto *row = new QHBoxLayout;
    row->addStretch(1);
    row->addWidget(run_);
    page->addLayout(row);
    page->addStretch(1);

    connect(mode_, &QComboBox::currentIndexChanged, this, [this] { onModeChanged(); });
    connect(noCompress_, &QCheckBox::toggled, this, [this] { onModeChanged(); });
    connect(src_, &PathRow::textChanged, this, [this] { suggestOutput(); });
    connect(run_, &QPushButton::clicked, this, &PackPage::start);
    onModeChanged();
}

void PackPage::onRetranslate()
{
    src_->retranslate();
    dst_->retranslate();
    onModeChanged();
}

void PackPage::showEvent(QShowEvent *e)
{
    PageBase::showEvent(e);
    ftpUpload_->setEnabled(FtpUpload::isReady());   // настройки FTP могли поменяться в «Настройках»
    if (FtpUpload::isReady()) ftpUpload_->setChecked(AppPaths::settings().value("ftp/enable", true).toBool());
}

void PackPage::onModeChanged()
{
    const QString m = code(mode_);
    const bool isFile = (m == "file");
    src_->setMode(isFile ? PathRow::OpenFile : PathRow::OpenDir);
    src_->setFilter(TR("pack.filterSrcFile"));
    dst_->setFilter(m == "exfat" ? TR("pack.filterExfat") : (m == "folder-raw" ? TR("pack.filterPfs") : TR("pack.filterPfsc")));
    const bool exfat = (m == "exfat");
    version_->setEnabled(!exfat);
    cluster_->setEnabled(exfat);
    level_->setEnabled(!exfat && !noCompress_->isChecked());
    noCompress_->setEnabled(!exfat);
    caseSens_->setEnabled(!exfat);
    verify_->setEnabled(!exfat);
    const char *hintKey = m == "folder" ? "pack.hint.folder" : m == "folder-raw" ? "pack.hint.raw"
                          : m == "file" ? "pack.hint.file" : "pack.hint.exfat";
    hint_->setText(I18n::instance().t(QString::fromLatin1(hintKey)));
    suggestOutput();
}

void PackPage::suggestOutput()
{
    const QString s = src_->text();
    if (s.isEmpty() || !dst_->text().isEmpty()) return;
    const QFileInfo fi(s);
    const QString m = code(mode_);
    const QString ext = (m == "exfat") ? "exfat" : (m == "folder-raw" ? "ffpfs" : "ffpfsc");
    dst_->setText(QDir::toNativeSeparators(fi.absoluteDir().absoluteFilePath(fi.completeBaseName().isEmpty() ? fi.fileName() : fi.completeBaseName()) + "." + ext));
}

void PackPage::setPath(const QString &path)
{
    mode_->setCurrentIndex(QFileInfo(path).isDir() ? 0 : 2);   // папка -> .ffpfsc, файл -> файл-в-образ
    dst_->setText(QString());
    src_->setText(path);
}

void PackPage::start()
{
    QString s = src_->text();
    const QString d = dst_->text();
    if (s.isEmpty() || !QFileInfo::exists(s)) { QMessageBox::warning(this, TR("app.name"), TR("err.noSource")); return; }
    if (d.isEmpty()) { QMessageBox::warning(this, TR("app.name"), TR("err.noOutput")); return; }

    const QString m = code(mode_);
    const bool fromFolder = (m == "folder" || m == "folder-raw" || m == "exfat");
    bool gameRootOk = false;
    if (fromFolder) {
        // Самая частая причина «образ упаковался, а на PS5 не запускается»: выбран не корень игры.
        // В корне должны лежать eboot.bin и sce_sys\param.json.
        const GameRootCheck chk = checkGameRoot(s);
        gameRootOk = chk.ok;
        if (!chk.ok) {
            if (chk.candidates.size() == 1) {
                const QString cand = QDir::toNativeSeparators(chk.candidates.first());
                if (QMessageBox::question(this, TR("app.name"), I18n::instance().t("pack.root.found", {cand})) != QMessageBox::Yes)
                    return;
                s = cand;
                src_->setText(cand);
                gameRootOk = true;
            } else {
                QStringList miss;
                if (!chk.hasEboot) miss << "eboot.bin";
                if (!chk.hasParam) miss << "sce_sys\\param.json";
                if (QMessageBox::warning(this, TR("app.name"),
                                         I18n::instance().t("pack.root.missing", {miss.join(QStringLiteral(", "))}),
                                         QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
                    return;
            }
        }
    }

    // Дальше — проверки, которые часто объясняют «образ собрался, а игра падает при старте».
    bool amprReady = false;          // эмулятор лежит в папке игры (был или добавлен сейчас)
    QString amprAdded;               // версия, добавленная этим запуском
    if (fromFolder && gameRootOk) {
        // 1) служебные файлы пакета в sce_sys (остаются после распаковки FPKG) — в образ-папку они не входят
        const QStringList leftovers = GameCheck::findPackageOnly(s);
        if (!leftovers.isEmpty()) {
            QMessageBox box(QMessageBox::Warning, TR("app.name"),
                            I18n::instance().t("pack.pkgOnly", {leftovers.mid(0, 8).join(QStringLiteral(", ")) + (leftovers.size() > 8 ? QStringLiteral(", …") : QString())}),
                            QMessageBox::NoButton, this);
            auto *bMove = box.addButton(TR("pack.pkgOnly.move"), QMessageBox::AcceptRole);
            box.addButton(TR("pack.pkgOnly.keep"), QMessageBox::DestructiveRole);
            auto *bCancel = box.addButton(TR("common.cancel"), QMessageBox::RejectRole);
            box.exec();
            if (box.clickedButton() == bCancel) return;
            if (box.clickedButton() == bMove) {
                QString backup, err;
                if (!GameCheck::movePackageOnly(s, leftovers, &backup, &err)) {
                    QMessageBox::critical(this, TR("app.name"), I18n::instance().t("pack.pkgOnly.failed", {err}));
                    return;
                }
                QMessageBox::information(this, TR("app.name"), I18n::instance().t("pack.pkgOnly.moved", {QDir::toNativeSeparators(backup)}));
            }
        }

        // 1б) папка «decrypted» (остаётся после Dump Runner) — исходные бинарники, в образе не нужны
        if (GameCheck::hasDecryptedFolder(s)) {
            QMessageBox box(QMessageBox::Warning, TR("app.name"), TR("pack.decrypted"), QMessageBox::NoButton, this);
            auto *bMove = box.addButton(TR("pack.decrypted.move"), QMessageBox::AcceptRole);
            box.addButton(TR("pack.pkgOnly.keep"), QMessageBox::DestructiveRole);
            auto *bCancel = box.addButton(TR("common.cancel"), QMessageBox::RejectRole);
            box.exec();
            if (box.clickedButton() == bCancel) return;
            if (box.clickedButton() == bMove) {
                QString moved, err;
                if (!GameCheck::moveDecryptedFolder(s, &moved, &err)) {
                    QMessageBox::critical(this, TR("app.name"), I18n::instance().t("pack.pkgOnly.failed", {err}));
                    return;
                }
                QMessageBox::information(this, TR("app.name"), I18n::instance().t("pack.decrypted.moved", {QDir::toNativeSeparators(moved)}));
            }
        }

        // 2) эмулятор AMPR: игра зовёт libSceAmpr, а модуля в папке нет
        const GameCheck::AmprStatus ampr = GameCheck::inspectAmpr(s);
        amprReady = ampr.modulePresent;
        const QList<GameCheck::AmprBuild> avail = GameCheck::availableAmprBuilds();
        const QString prefVersion = AppPaths::settings().value("pack/amprVersion", "0.3.6.6").toString();
        const GameCheck::AmprBuild *autoBuild = nullptr;
        for (const GameCheck::AmprBuild &b : avail) if (b.version == prefVersion) autoBuild = &b;
        const bool forceAmpr = AppPaths::settings().value("pack/amprForce", false).toBool();
        const bool silentAdd = (ampr.needsModule() && AppPaths::settings().value("pack/amprAuto", false).toBool())
                               || (forceAmpr && !ampr.modulePresent);
        if (silentAdd && autoBuild) {
            // Автоматический режим (Настройки): без вопроса добавляем выбранную версию.
            // «Всегда добавлять» работает и когда eboot.bin не упоминает libSceAmpr (проверка могла не сработать).
            QString err;
            if (!GameCheck::restoreAmpr(s, *autoBuild, &err)) {
                QMessageBox::critical(this, TR("app.name"), I18n::instance().t("ampr.failed", {err}));
                return;
            }
            amprReady = true;
            amprAdded = autoBuild->version;
        } else if (ampr.needsModule()) {
            QMessageBox box(QMessageBox::Warning, TR("app.name"), TR("ampr.missing"), QMessageBox::NoButton, this);
            QPushButton *bAdd = avail.isEmpty() ? nullptr : box.addButton(TR("ampr.add"), QMessageBox::AcceptRole);
            box.addButton(TR("ampr.skip"), QMessageBox::DestructiveRole);
            auto *bCancel = box.addButton(TR("common.cancel"), QMessageBox::RejectRole);
            if (!bAdd) box.setInformativeText(TR("ampr.noBundled"));
            box.exec();
            if (box.clickedButton() == bCancel) return;
            if (bAdd && box.clickedButton() == bAdd) {
                QStringList labels;
                for (const GameCheck::AmprBuild &b : avail) labels << I18n::instance().t(b.labelKey, {b.version});
                bool ok = false;
                const QString pick = QInputDialog::getItem(this, TR("app.name"), TR("ampr.pickVersion"), labels, 0, false, &ok);
                if (!ok) return;
                const GameCheck::AmprBuild &chosen = avail.at(qMax(0, int(labels.indexOf(pick))));
                QString err;
                if (!GameCheck::restoreAmpr(s, chosen, &err)) {
                    QMessageBox::critical(this, TR("app.name"), I18n::instance().t("ampr.failed", {err}));
                    return;
                }
                amprReady = true;
                amprAdded = chosen.version;
                QMessageBox::information(this, TR("app.name"), I18n::instance().t("ampr.added", {chosen.version}));
            }
        }
    }

    // 3) «чистый PFS» со сжатием: проверка проходит, но консоль может читать такой образ неверно
    if (m == "folder-raw" && !noCompress_->isChecked()) {
        if (QMessageBox::warning(this, TR("app.name"), TR("pack.rawWarn"), QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
            return;
    }

    if (QFileInfo(d).isFile()) {
        if (QMessageBox::question(this, TR("app.name"), I18n::instance().t("err.overwrite", {d})) != QMessageBox::Yes) return;
        QFile::remove(d);
    }
    QDir().mkpath(QFileInfo(d).absolutePath());

    QStringList a{QStringLiteral("pack")};
    if (m == "file") a << "file";
    else if (m == "exfat") a << "exfat";
    else { a << "folder"; if (m == "folder-raw") a << "--raw"; }
    if (m != "exfat") {
        a << "--version" << code(version_);
        if (noCompress_->isChecked()) a << "--no-compress";
        else a << "--compression-level" << QString::number(level_->value());
        if (caseSens_->isChecked()) a << "--case-sensitive";
        if (verify_->isChecked()) a << "--verify";
        if ((m == "folder" || m == "folder-raw") && gameRootOk) a << "--require-game-files";
    }
    if (m == "exfat" && code(cluster_) != QLatin1String("auto")) a << "--cluster-size" << code(cluster_);
    if (verbose_->isChecked()) a << "--verbose";
    a << s << d;

    QString title = TR("pack.jobTitle");
    if (!amprAdded.isEmpty()) title += QStringLiteral(" · AMPR ") + amprAdded;
    QList<JobSpec> jobs{JobSpec{title, AppPaths::mkpfsExe(), a, QString(), 20}};
    // MkPFS создаёт ampr_emu.index только при упаковке «папка → PFS/FFPFSC». Для чистого exFAT индекс пересоздаём
    // отдельным шагом (ps5aio-helper), если эмулятор лежит в папке игры.
    if (m == "exfat" && amprReady && AppPaths::helperAvailable())
        jobs << JobSpec{TR("pack.job.ampr"), AppPaths::helperExe(), {QStringLiteral("refresh-ampr"), d}, QString(), 1};
    if (ftpUpload_->isChecked()) jobs << FtpUpload::makeJob(d);
    emit runRequested(jobs, QFileInfo(d).absolutePath());
}
