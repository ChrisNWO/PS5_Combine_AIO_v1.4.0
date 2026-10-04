#include "BackportPage.h"

#include <QDir>
#include <QDirIterator>
#include <QStorageInfo>
#include <QFileInfo>
#include <QFormLayout>
#include <QMessageBox>

#include "AppPaths.h"
#include "ParamJson.h"

// Пары версий SDK: номер пары = мажорная версия прошивки PS5. В Auto-Backpork таблица заканчивается на 10; пары 11-13
// добавляет сборка (scripts/build-tools.ps1) и на консоли они не проверены — в списке помечены «экспериментально».
static constexpr int kMaxPair = 13, kLastVerifiedPair = 10;

BackportPage::BackportPage(QWidget *parent) : PageBase(parent)
{
    auto *page = new QVBoxLayout(this);
    page->setContentsMargins(28, 24, 28, 12);
    page->setSpacing(14);
    header(page, "backport.title", "backport.subtitle");

    auto *c1 = card(page, "backport.cardPaths");
    auto *form = new QFormLayout;
    form->setHorizontalSpacing(16);
    form->setVerticalSpacing(10);
    src_ = new PathRow(PathRow::OpenDir);
    dst_ = new PathRow(PathRow::OpenDir);
    fakelib_ = new PathRow(PathRow::OpenDir);
    fakelib_->setText(AppPaths::settings().value("backport/fakelib").toString());
    form->addRow(R(new QLabel, "backport.source"), src_);
    form->addRow(R(new QLabel, "backport.output"), dst_);
    form->addRow(R(new QLabel, "backport.fakelib"), fakelib_);
    c1->addLayout(form);
    info_ = new QLabel;
    info_->setObjectName("muted");
    info_->setWordWrap(true);
    c1->addWidget(info_);

    auto *c2 = card(page, "backport.cardOptions");
    auto *f2 = new QFormLayout;
    f2->setHorizontalSpacing(16);
    mode_ = new QComboBox;
    fillCombo(mode_, {{"auto", "backport.mode.auto"}, {"downgrade", "backport.mode.downgrade"}, {"decrypt", "backport.mode.decrypt"},
                      {"fself", "backport.mode.fself"}});
    target_ = new QComboBox;
    for (int n = 1; n <= kMaxPair; ++n) {
        QString label = QStringLiteral("%1.xx  (PS5 SDK %1.00)").arg(n);
        if (n > kLastVerifiedPair) label += QStringLiteral(" · ") + TR("backport.pairExperimental");
        target_->addItem(label, n);
    }
    // по умолчанию — мажорная версия прошивки консоли из «Настроек», иначе 4 (умолчание Auto-Backpork)
    int pair = fwToNumber(AppPaths::settings().value("console/fw").toString()) / 100;
    if (pair < 1 || pair > kMaxPair) pair = AppPaths::settings().value("backport/pair", 4).toInt();
    target_->setCurrentIndex(qBound(0, pair - 1, kMaxPair - 1));
    f2->addRow(R(new QLabel, "backport.mode"), mode_);
    f2->addRow(R(new QLabel, "backport.target"), target_);
    c2->addLayout(f2);
    noLibc_ = R(new QCheckBox, "backport.noLibc");
    noBackup_ = R(new QCheckBox, "backport.noBackup");
    overwrite_ = R(new QCheckBox, "backport.overwrite");
    c2->addWidget(noLibc_);
    c2->addWidget(noBackup_);
    c2->addWidget(overwrite_);

    auto *notice = R(new QLabel, "backport.notice");
    notice->setObjectName("muted");
    notice->setWordWrap(true);
    page->addWidget(notice);

    run_ = R(new QPushButton, "backport.run");
    run_->setObjectName("primary");
    auto *row = new QHBoxLayout;
    row->addStretch(1);
    row->addWidget(run_);
    page->addLayout(row);
    page->addStretch(1);

    connect(src_, &PathRow::textChanged, this, [this] { suggestOutput(); updateInfo(); });
    connect(fakelib_, &PathRow::textChanged, this, [](const QString &t) { AppPaths::settings().setValue("backport/fakelib", t.trimmed()); });
    connect(target_, &QComboBox::currentIndexChanged, this, [this] { AppPaths::settings().setValue("backport/pair", target_->currentData().toInt()); });
    connect(mode_, &QComboBox::currentIndexChanged, this, [this] { updateModeUi(); });
    connect(run_, &QPushButton::clicked, this, &BackportPage::start);
    onRetranslate();
}

void BackportPage::onRetranslate()
{
    src_->retranslate();
    dst_->retranslate();
    fakelib_->retranslate();
    updateInfo();
    updateModeUi();
}

// «Только fake-подпись»: SDK-пара и fakelib не нужны, «Куда сохранить» можно оставить пустым (подпись на месте).
void BackportPage::updateModeUi()
{
    const QString m = code(mode_);
    const bool fself = (m == QLatin1String("fself"));
    target_->setEnabled(!fself);
    fakelib_->setEnabled(m == QLatin1String("auto") || m == QLatin1String("downgrade"));
    noLibc_->setEnabled(!fself && m != QLatin1String("decrypt"));
    noBackup_->setEnabled(!fself && m != QLatin1String("decrypt"));
    dst_->setPlaceholder(fself ? TR("backport.outOptional") : QString());
}

void BackportPage::setPath(const QString &path)
{
    dst_->setText(QString());
    src_->setText(path);
}

// Результат кладём рядом с исходной папкой: «<игра>_backport».
void BackportPage::suggestOutput()
{
    const QString s = src_->text();
    if (s.isEmpty() || !dst_->text().isEmpty()) return;
    const QFileInfo fi(s);
    dst_->setText(QDir::toNativeSeparators(fi.absoluteDir().absoluteFilePath(fi.fileName() + QStringLiteral("_backport"))));
}

void BackportPage::updateInfo()
{
    const QString s = src_->text();
    if (s.isEmpty() || !QFileInfo(s).isDir()) { info_->setText(TR("backport.infoHint")); return; }
    const GameInfo g = readGameInfo(s);
    if (!g.found) { info_->setText(TR("backport.infoNoParam")); return; }
    QString t = I18n::instance().t("backport.info", {g.title, g.titleId, g.requiredFw.isEmpty() ? QStringLiteral("—") : g.requiredFw,
                                                     g.sdk.isEmpty() ? QStringLiteral("—") : g.sdk});
    const QString cons = AppPaths::settings().value("console/fw").toString().trimmed();
    if (!cons.isEmpty()) t += QLatin1Char(' ') + I18n::instance().t("backport.infoConsole", {cons});
    info_->setText(t);
}

static qint64 folderBytes(const QString &root)
{
    qint64 n = 0;
    QDirIterator it(root, QDir::Files | QDir::Hidden | QDir::System | QDir::NoSymLinks, QDirIterator::Subdirectories);
    while (it.hasNext()) { it.next(); n += it.fileInfo().size(); }
    return n;
}

void BackportPage::start()
{
    const QString m = code(mode_);
    const bool fself = (m == QLatin1String("fself"));
    if (!QFileInfo::exists(fself ? AppPaths::fselfExe() : AppPaths::backporkExe())) {
        QMessageBox::information(this, TR("app.name"), fself ? TR("backport.needFself") : TR("backport.needTool"));
        return;
    }
    const QString s = src_->text(), d = dst_->text(), fl = fakelib_->text();
    if (s.isEmpty() || !QFileInfo(s).isDir()) { QMessageBox::warning(this, TR("app.name"), TR("err.noSource")); return; }
    if (d.isEmpty() && !fself) { QMessageBox::warning(this, TR("app.name"), TR("err.noOutput")); return; }

    // Выходная папка не должна совпадать с исходной или лежать внутри неё (иначе обработка зациклится на своих же файлах).
    const QString cs = QDir::cleanPath(QFileInfo(s).absoluteFilePath()).toLower();
    const QString cd = QDir::cleanPath(QFileInfo(d).absoluteFilePath()).toLower();
    if (cd == cs || cd.startsWith(cs + QLatin1Char('/'))) { QMessageBox::warning(this, TR("app.name"), TR("backport.outInside")); return; }

    if (fself) {
        if (d.isEmpty()) {          // подпись на месте: исходный дамп изменяется
            if (QMessageBox::warning(this, TR("app.name"), TR("backport.inPlace"), QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
                return;
        } else {                    // копия всего дампа: хватит ли места
            QString probe = QFileInfo(d).absoluteFilePath();
            while (!QFileInfo::exists(probe) && probe != QFileInfo(probe).absolutePath()) probe = QFileInfo(probe).absolutePath();
            const qint64 need = folderBytes(s), have = QStorageInfo(probe).bytesAvailable();
            if (have >= 0 && have < need + need / 50) {
                QMessageBox::warning(this, TR("app.name"), I18n::instance().t("backport.noSpace", {QString::number(need / (1LL << 30)), QString::number(have / (1LL << 30))}));
                return;
            }
        }
    }

    // Первый запуск: напоминание об условиях использования.
    if (!AppPaths::settings().value("backport/ack", false).toBool()) {
        if (QMessageBox::question(this, TR("app.name"), TR("backport.ack")) != QMessageBox::Yes) return;
        AppPaths::settings().setValue("backport/ack", true);
    }

    if (m == QLatin1String("auto") || m == QLatin1String("downgrade")) {
        if (fl.isEmpty()) {
            if (QMessageBox::warning(this, TR("app.name"), TR("backport.noFakelib"), QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
                return;
        } else if (!QFileInfo(fl).isDir() || QDir(fl).entryList(QDir::Files | QDir::NoDotAndDotDot).isEmpty()) {
            QMessageBox::warning(this, TR("app.name"), TR("backport.fakelibEmpty"));
            return;
        }
    }

    if (fself) {
        QStringList fa{QStringLiteral("--input"), s};
        if (!d.isEmpty()) fa << QStringLiteral("--output") << d;
        if (overwrite_->isChecked()) fa << QStringLiteral("--overwrite");
        emit runRequested({JobSpec{TR("backport.jobFself"), AppPaths::fselfExe(), fa, QString(), 1}}, d.isEmpty() ? s : d);
        return;
    }
    QStringList a{QStringLiteral("--mode"), m, QStringLiteral("--input"), s, QStringLiteral("--output"), d,
                  QStringLiteral("--sdk-pair"), QString::number(target_->currentData().toInt()), QStringLiteral("--no-colors")};
    if (m != QLatin1String("decrypt") && !fl.isEmpty()) a << QStringLiteral("--fakelib") << fl;
    if (noLibc_->isChecked()) a << QStringLiteral("--no-libc-patch");
    if (noBackup_->isChecked()) a << QStringLiteral("--no-backup");
    if (overwrite_->isChecked()) a << QStringLiteral("--overwrite");
    emit runRequested({JobSpec{TR("backport.job"), AppPaths::backporkExe(), a, QString(), 1}}, d);
}
