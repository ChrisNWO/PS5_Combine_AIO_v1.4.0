#include "FpkgPage.h"

#include <QFileInfo>
#include <QMessageBox>
#include <QStyle>

#include "AppPaths.h"
#include "ParamJson.h"
#include "FtpUpload.h"
#include "PkgSizeEstimate.h"

#include <QDirIterator>
#include <QRegularExpression>

void FpkgPage::setState(QLabel *l, const char *objectName)
{
    l->setObjectName(QString::fromLatin1(objectName));
    l->style()->unpolish(l);
    l->style()->polish(l);
}

FpkgPage::FpkgPage(QWidget *parent) : PageBase(parent)
{
    auto *page = new QVBoxLayout(this);
    page->setContentsMargins(28, 24, 28, 12);
    page->setSpacing(14);
    header(page, "fpkg.title", "fpkg.subtitle");

    // ---------- источник и результат ----------
    auto *c1 = card(page, "fpkg.cardSource");
    srcForm_ = new QFormLayout;
    srcForm_->setHorizontalSpacing(16);
    srcForm_->setVerticalSpacing(10);
    srcMode_ = new QComboBox;
    fillCombo(srcMode_, {{"folder", "fpkg.srcMode.folder"}, {"image", "fpkg.srcMode.image"}});
    srcLabel_ = new QLabel;
    src_ = new PathRow(PathRow::OpenDir);
    out_ = new PathRow(PathRow::OpenDir);
    exfatMode_ = new QComboBox;
    fillCombo(exfatMode_, {{"extract", "fpkg.exfat.extract"}, {"auto", "fpkg.exfat.auto"}, {"mount", "fpkg.exfat.mount"}});
    temp_ = new PathRow(PathRow::OpenDir);
    srcForm_->addRow(R(new QLabel, "fpkg.srcMode"), srcMode_);
    srcForm_->addRow(srcLabel_, src_);
    srcForm_->addRow(R(new QLabel, "fpkg.output"), out_);
    srcForm_->addRow(R(new QLabel, "fpkg.exfatMode"), exfatMode_);
    srcForm_->addRow(R(new QLabel, "fpkg.temp"), temp_);
    c1->addLayout(srcForm_);

    // данные из param.json — нативный C++-разбор (ParamJson.cpp)
    auto *g = new QFormLayout;
    g->setHorizontalSpacing(16);
    g->setVerticalSpacing(4);
    iTitle_ = new QLabel("—");
    iTitleId_ = new QLabel("—");
    iContent_ = new QLabel("—");
    iVersion_ = new QLabel("—");
    iFw_ = new QLabel("—");
    iSdk_ = new QLabel("—");
    for (QLabel *l : {iTitle_, iTitleId_, iContent_, iVersion_, iFw_, iSdk_}) l->setTextInteractionFlags(Qt::TextSelectableByMouse);
    g->addRow(R(new QLabel, "fpkg.info.title"), iTitle_);
    g->addRow(R(new QLabel, "fpkg.info.titleId"), iTitleId_);
    g->addRow(R(new QLabel, "fpkg.info.contentId"), iContent_);
    g->addRow(R(new QLabel, "fpkg.info.version"), iVersion_);
    g->addRow(R(new QLabel, "fpkg.info.fw"), iFw_);
    g->addRow(R(new QLabel, "fpkg.info.sdk"), iSdk_);
    c1->addLayout(g);
    engine_ = new QComboBox;
    fillCombo(engine_, {{"builtin", "fpkg.engine.builtin"}, {"sdk279", "fpkg.engine.sdk279"}});
    srcForm_->addRow(R(new QLabel, "fpkg.engine"), engine_);
    sizeEstimate_ = new QLabel;
    sizeEstimate_->setObjectName("muted");
    sizeEstimate_->setWordWrap(true);
    c1->addWidget(sizeEstimate_);
    fwNote_ = new QLabel;
    fwNote_->setWordWrap(true);
    imageNote_ = new QLabel;
    imageNote_->setObjectName("muted");
    imageNote_->setWordWrap(true);
    warn_ = new QLabel;
    warn_->setObjectName("warn");
    warn_->setWordWrap(true);
    c1->addWidget(fwNote_);
    c1->addWidget(imageNote_);
    c1->addWidget(warn_);

    // ---------- параметры пакета ----------
    auto *c2 = card(page, "fpkg.cardOptions");
    optForm_ = new QFormLayout;
    optForm_->setHorizontalSpacing(16);
    optForm_->setVerticalSpacing(10);
    kind_ = new QComboBox;
    fillCombo(kind_, {{"app", "fpkg.kind.app"}, {"homebrew", "fpkg.kind.homebrew"}, {"dlc", "fpkg.kind.dlc"}});
    preset_ = new QComboBox;
    fillCombo(preset_, {{"balanced", "fpkg.preset.balanced"}, {"fast", "fpkg.preset.fast"},
                        {"smallest", "fpkg.preset.smallest"}, {"maximum", "fpkg.preset.maximum"}});
    contentId_ = new QLineEdit;
    title_ = new QLineEdit;
    version_ = new QLineEdit;
    version_->setPlaceholderText("NN.NNN.NNN");
    optForm_->addRow(R(new QLabel, "fpkg.kind"), kind_);
    optForm_->addRow(R(new QLabel, "fpkg.preset"), preset_);
    optForm_->addRow(R(new QLabel, "fpkg.contentIdOverride"), contentId_);
    optForm_->addRow(R(new QLabel, "fpkg.titleOverride"), title_);
    optForm_->addRow(R(new QLabel, "fpkg.versionOverride"), version_);
    c2->addLayout(optForm_);
    fullVerify_ = R(new QCheckBox, "fpkg.fullVerify");
    sha_ = R(new QCheckBox, "fpkg.sha256");
    overwrite_ = R(new QCheckBox, "fpkg.overwrite");
    clean_ = R(new QCheckBox, "fpkg.cleanJunk");
    noSdk_ = R(new QCheckBox, "fpkg.noSonySdk");
    overwrite_->setChecked(true);
    ftpUpload_ = R(new QCheckBox, "fpkg.ftpUpload");
    ftpUpload_->setChecked(FtpUpload::isReady());
    ftpUpload_->setEnabled(FtpUpload::isReady());
    ftpUpload_->setToolTip(TR("pack.ftpUploadTip"));
    for (QCheckBox *c : {fullVerify_, sha_, overwrite_, clean_, noSdk_, ftpUpload_}) c2->addWidget(c);

    // ---------- прошивка и совместимость ----------
    auto *c3 = card(page, "fpkg.cardCompat");
    auto *hint = R(new QLabel, "fpkg.compatHint");
    hint->setObjectName("muted");
    hint->setWordWrap(true);
    c3->addWidget(hint);
    auto *f3 = new QFormLayout;
    f3->setHorizontalSpacing(16);
    sdk_ = new QComboBox;
    sdk_->addItem(TR("fpkg.sdk.auto"), 0);
    for (int i = 1; i <= 11; ++i) sdk_->addItem(QString::number(i), i);
    f3->addRow(R(new QLabel, "fpkg.sdkOverride"), sdk_);
    c3->addLayout(f3);
    keepFw_ = R(new QCheckBox, "fpkg.keepFw");
    c3->addWidget(keepFw_);

    // ---------- SDK 2.79: AC-пакеты, патч, PlayGo (видно только при выборе этого движка) ----------
    auto *c4 = card(page, "fpkg.card279");
    auto *hint279 = R(new QLabel, "fpkg.hint279");
    hint279->setObjectName("muted");
    hint279->setWordWrap(true);
    c4->addWidget(hint279);
    auto *f4 = new QFormLayout;
    f4->setHorizontalSpacing(16);
    volume_ = new QComboBox;
    fillCombo(volume_, {{"app", "fpkg.volume.app"}, {"ac", "fpkg.volume.ac"}});
    entitlementKey_ = new QLineEdit;
    entitlementKey_->setPlaceholderText(QStringLiteral("32 hex, напр. 0123456789ABCDEF0123456789ABCDEF"));
    playgoLangs_ = new QLineEdit;
    playgoLangs_->setPlaceholderText(QStringLiteral("en-US,ru-RU  (пусто — языки из источника)"));
    reference_ = new PathRow(PathRow::OpenFile);
    reference_->setFilter(QStringLiteral("PKG (*.pkg)"));
    f4->addRow(R(new QLabel, "fpkg.volume"), volume_);
    f4->addRow(R(new QLabel, "fpkg.entitlementKey"), entitlementKey_);
    f4->addRow(R(new QLabel, "fpkg.playgoLangs"), playgoLangs_);
    f4->addRow(R(new QLabel, "fpkg.reference"), reference_);
    c4->addLayout(f4);
    keepIntermediate279_ = R(new QCheckBox, "fpkg.keepIntermediate279");
    c4->addWidget(keepIntermediate279_);
    card279_ = c4;

    run_ = R(new QPushButton, "fpkg.run");
    run_->setObjectName("primary");
    auto *row = new QHBoxLayout;
    row->addStretch(1);
    row->addWidget(run_);
    page->addLayout(row);
    page->addStretch(1);

    connect(engine_, &QComboBox::currentIndexChanged, this, [this] { onEngineChanged(); });
    connect(volume_, &QComboBox::currentIndexChanged, this, [this] { onEngineChanged(); });
    connect(src_, &PathRow::textChanged, this, [this] { updateSizeEstimate(); });
    connect(srcMode_, &QComboBox::currentIndexChanged, this, [this] { onSourceModeChanged(); });
    connect(src_, &PathRow::textChanged, this, [this] { refreshInfo(); });
    connect(noSdk_, &QCheckBox::toggled, this, [this] { refreshInfo(); });
    connect(run_, &QPushButton::clicked, this, &FpkgPage::start);
    onSourceModeChanged();
    onEngineChanged();
}

void FpkgPage::setPath(const QString &path)
{
    srcMode_->setCurrentIndex(QFileInfo(path).isDir() ? 0 : 1);
    src_->setText(path);
}

void FpkgPage::onSourceModeChanged()
{
    const bool image = (code(srcMode_) == "image");
    src_->setMode(image ? PathRow::OpenFile : PathRow::OpenDir);
    src_->setFilter(TR("fpkg.filterImage"));
    srcLabel_->setText(image ? TR("fpkg.sourceFile") : TR("fpkg.source"));   // TR() принимает только литерал
    srcForm_->setRowVisible(exfatMode_, image);
    srcForm_->setRowVisible(temp_, image);
    refreshInfo();
    updateSizeEstimate();
}

void FpkgPage::onRetranslate()
{
    sdk_->setItemText(0, TR("fpkg.sdk.auto"));
    src_->retranslate();
    out_->retranslate();
    temp_->retranslate();
    reference_->retranslate();
    onSourceModeChanged();
    onEngineChanged();
}

void FpkgPage::showEvent(QShowEvent *e)
{
    PageBase::showEvent(e);
    ftpUpload_->setEnabled(FtpUpload::isReady());
    if (FtpUpload::isReady()) ftpUpload_->setChecked(AppPaths::settings().value("ftp/enable", true).toBool());
}

void FpkgPage::refreshInfo()
{
    auto &i = I18n::instance();
    const QString s = src_->text();
    const bool image = (code(srcMode_) == "image");
    const bool isDir = !image && QFileInfo(s).isDir();
    GameInfo g;
    if (isDir) g = readGameInfo(s);
    auto v = [](const QString &x) { return x.isEmpty() ? QStringLiteral("—") : x; };
    iTitle_->setText(v(g.title));
    iTitleId_->setText(v(g.titleId));
    iContent_->setText(v(g.contentId));
    iVersion_->setText(v(g.version));
    iFw_->setText(v(g.requiredFw));
    iSdk_->setText(v(g.sdk));
    contentId_->setPlaceholderText(g.contentId);
    title_->setPlaceholderText(g.title);
    imageNote_->setText(image && !s.isEmpty() ? TR("fpkg.imageNote") : QString());
    imageNote_->setVisible(!imageNote_->text().isEmpty());

    // сравнение с прошивкой консоли (задаётся в «Настройках»)
    const QString cons = AppPaths::settings().value("console/fw").toString().trimmed();
    const int need = fwToNumber(g.requiredFw), have = fwToNumber(cons);
    QString note;
    if (need > 0) {
        if (have > 0 && need > have) { note = i.t("lib.fw.tooHigh", {g.requiredFw, cons}); setState(fwNote_, "warn"); }
        else if (have > 0)           { note = i.t("lib.fw.ok", {cons});                    setState(fwNote_, "ok"); }
        else                         { note = TR("lib.fw.noConsole");                      setState(fwNote_, "muted"); }
    }
    fwNote_->setText(note);
    fwNote_->setVisible(!note.isEmpty());

    QString w;
    if (isDir && !g.found) w = TR("fpkg.warn.noParam");
    else if (isDir && !g.keystoneOk && !noSdk_->isChecked()) w = TR("fpkg.warn.keystone");
    warn_->setText(w);
    warn_->setVisible(!w.isEmpty());
}

// Карточка SDK 2.79 видна только когда выбран этот движок; AC скрывает чисто APP-поля (keep-fw/sdk override/preset/kind
// в текущей версии остаются общими и для 2.79 не используются — игнорируются, а не дублируются в интерфейсе).
void FpkgPage::onEngineChanged()
{
    const bool sdk279 = (code(engine_) == QLatin1String("sdk279"));
    card279_->parentWidget()->setVisible(sdk279);   // QFrame, которым владеет этот layout (см. PageBase::card)
    const bool ac = sdk279 && code(volume_) == QLatin1String("ac");
    entitlementKey_->setEnabled(ac);
    playgoLangs_->setEnabled(sdk279 && !ac);
    if (!sdk279) return;
    if (!AppPaths::fpkg279Available())
        imageNote_->setText(TR("fpkg.engine.sdk279.missing"));
}

// Оценка требуемого уровня «большого пакета» (attributePub) по размеру НЕупакованных файлов — чисто
// арифметика по документированным порогам Prospero SDK (PkgSizeEstimate.cpp), без обращения к Sony SDK.
void FpkgPage::updateSizeEstimate()
{
    const QString s = src_->text();
    if (code(srcMode_) != QLatin1String("folder") || s.isEmpty() || !QFileInfo(s).isDir()) {
        sizeEstimate_->clear();
        return;
    }
    qint64 bytes = 0;
    int files = 0;
    QDirIterator it(s, QDir::Files | QDir::Hidden | QDir::System | QDir::NoSymLinks, QDirIterator::Subdirectories);
    while (it.hasNext()) { it.next(); bytes += it.fileInfo().size(); ++files; }
    const PkgSizeEstimate::Result r = PkgSizeEstimate::estimate(bytes, files, code(engine_) == QLatin1String("sdk279"));
    auto fmtGb = [](qint64 b) { return QString::number(double(b) / (1LL << 30), 'f', 1); };
    QString levelText = r.attributePubLevel == 4
        ? I18n::instance().t("fpkg.size.lv4", {QString::number(r.appSizeInGib)})
        : QString::number(r.attributePubLevel);
    QString t = I18n::instance().t("fpkg.size.estimate", {fmtGb(bytes), fmtGb(r.estimatedBytes), levelText});
    if (r.mountLevel > 0) t += QLatin1Char(' ') + I18n::instance().t("fpkg.size.mountLevel", {QString::number(r.mountLevel)});
    if (r.fileCountExceeded) t += QLatin1Char(' ') + TR("fpkg.size.tooManyFiles");
    sizeEstimate_->setText(t);
}

void FpkgPage::start()
{
    const QString s = src_->text(), o = out_->text();
    const bool image = (code(srcMode_) == "image");
    if (s.isEmpty() || !QFileInfo::exists(s)) { QMessageBox::warning(this, TR("app.name"), TR("err.noSource")); return; }
    if (o.isEmpty()) { QMessageBox::warning(this, TR("app.name"), TR("err.noOutput")); return; }
    QDir().mkpath(o);

    if (code(engine_) == QLatin1String("sdk279")) { startSdk279(s, o); return; }

    QStringList a{"build", "--source", s, "--output", o, "--kind", code(kind_), "--preset", code(preset_),
                  "--lang", "en", "--quiet"};
    if (!contentId_->text().trimmed().isEmpty()) a << "--content-id" << contentId_->text().trimmed();
    if (!title_->text().trimmed().isEmpty()) a << "--title" << title_->text().trimmed();
    if (!version_->text().trimmed().isEmpty()) a << "--version" << version_->text().trimmed();
    if (fullVerify_->isChecked()) a << "--full-verify";
    if (sha_->isChecked()) a << "--sha256";
    a << (overwrite_->isChecked() ? "--overwrite" : "--keep-existing");
    if (clean_->isChecked()) a << "--clean-junk";

    if (image) {                                // образ: fpkg-cli сам распаковывает/монтирует и убирает временные файлы
        a << "--exfat" << code(exfatMode_);
        const QString t = temp_->text();
        if (!t.isEmpty()) { QDir().mkpath(t); a << "--temp" << t; }
    }

    // --keep-required-fw и --sdk работают ТОЛЬКО во встроенном движке: Sony SDK сам решает версию/PlayGo и игнорирует эти флаги.
    const int sdk = sdk_->currentData().toInt();
    bool fwFlags = keepFw_->isChecked() || sdk > 0;
    if (fwFlags && !noSdk_->isChecked()) {
        QStringList names;
        if (keepFw_->isChecked()) names << "--keep-required-fw";
        if (sdk > 0) names << "--sdk";
        QMessageBox box(QMessageBox::Question, TR("app.name"),
                        I18n::instance().t("fpkg.engineAsk", {names.join(QStringLiteral(", "))}),
                        QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel, this);
        box.button(QMessageBox::Yes)->setText(TR("fpkg.engine.switch"));
        box.button(QMessageBox::No)->setText(TR("fpkg.engine.keep"));
        box.button(QMessageBox::Cancel)->setText(TR("common.cancel"));
        box.setDefaultButton(QMessageBox::Yes);
        const int r = box.exec();
        if (r == QMessageBox::Cancel) return;
        if (r == QMessageBox::Yes) noSdk_->setChecked(true);
        else fwFlags = false;                   // остаёмся на Sony SDK — флаги всё равно были бы проигнорированы
    }
    if (noSdk_->isChecked()) {
        a << "--no-sony-sdk";
        if (fwFlags) {
            if (keepFw_->isChecked()) a << "--keep-required-fw";
            if (sdk > 0) a << "--sdk" << QString::number(sdk);
        }
    }
    QList<JobSpec> jobs{JobSpec{TR("fpkg.jobTitle"), AppPaths::fpkgCliExe(), a, QString(), 1}};
    if (ftpUpload_->isChecked()) {
        const QString stem = contentId_->text().trimmed().isEmpty() ? QFileInfo(s).fileName() : contentId_->text().trimmed();
        jobs << FtpUpload::makeJob(QDir(o).absoluteFilePath(stem + QStringLiteral(".pkg")));
    }
    emit runRequested(jobs, o);
}

// Альтернативный движок: SDK 2.79 plaintext-профиль (tools/fpkg279, см. README third_party) — отдельный GP5-генератор
// и прямой вызов prospero-pub-cmd.exe через build-from-folder.ps1. Даёт то, чего нет во встроенном движке: AC-пакеты
// (доп. контент с ключом доступа) и построение патча вместе с его .remastered.pkg за один проход.
void FpkgPage::startSdk279(const QString &s, const QString &o)
{
    if (!AppPaths::fpkg279Available()) {
        QMessageBox::information(this, TR("app.name"), TR("fpkg.engine.sdk279.missing"));
        return;
    }
    if (code(srcMode_) != QLatin1String("folder")) {
        QMessageBox::warning(this, TR("app.name"), TR("fpkg.engine.sdk279.folderOnly"));
        return;
    }
    const bool ac = (code(volume_) == QLatin1String("ac"));
    if (ac) {
        const QString key = entitlementKey_->text().trimmed();
        static const QRegularExpression hex32(QStringLiteral("^[0-9A-Fa-f]{32}$"));
        if (!hex32.match(key).hasMatch()) { QMessageBox::warning(this, TR("app.name"), TR("fpkg.entitlementKey.bad")); return; }
    } else {
        const GameInfo g = readGameInfo(s);
        if (!g.keystoneOk) { QMessageBox::warning(this, TR("app.name"), TR("fpkg.warn.keystone")); return; }
    }

    const QString stem = contentId_->text().trimmed().isEmpty() ? QFileInfo(s).fileName() : contentId_->text().trimmed();
    const QString outFile = QDir(o).absoluteFilePath(stem + QStringLiteral(".pkg"));
    if (QFileInfo::exists(outFile) && !overwrite_->isChecked()) {
        QMessageBox::warning(this, TR("app.name"), I18n::instance().t("err.overwrite", {outFile}));
        return;
    }

    QStringList a{QStringLiteral("-NoProfile"), QStringLiteral("-ExecutionPolicy"), QStringLiteral("Bypass"),
                  QStringLiteral("-File"), AppPaths::fpkg279Script(),
                  QStringLiteral("-SourceFolder"), s, QStringLiteral("-OutputPackage"), outFile,
                  QStringLiteral("-Volume"), code(volume_), QStringLiteral("-CompressionLevel"), QStringLiteral("7")};
    if (ac) {
        a << QStringLiteral("-EntitlementKey") << entitlementKey_->text().trimmed().toUpper();
    } else if (!playgoLangs_->text().trimmed().isEmpty()) {
        a << QStringLiteral("-PlayGoLanguages") << playgoLangs_->text().trimmed();
    }
    const QString ref = reference_->text();
    if (!ref.isEmpty()) a << QStringLiteral("-ReferencePackage") << ref;
    const QString t = temp_->text();
    if (!t.isEmpty()) { QDir().mkpath(t); a << QStringLiteral("-TemporaryDirectory") << t; }
    if (overwrite_->isChecked()) a << QStringLiteral("-Force");
    if (keepIntermediate279_->isChecked()) a << QStringLiteral("-KeepIntermediate");

    QList<JobSpec> jobs279{JobSpec{TR("fpkg.job279"), QStringLiteral("powershell.exe"), a, QString(), 1}};
    if (ftpUpload_->isChecked()) jobs279 << FtpUpload::makeJob(outFile);
    emit runRequested(jobs279, o);
}
