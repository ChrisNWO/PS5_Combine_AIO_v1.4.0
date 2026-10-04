#include "SettingsPage.h"

#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QFormLayout>
#include <QUrl>

#include "AppPaths.h"
#include "GameCheck.h"
#include "Theme.h"

SettingsPage::SettingsPage(QWidget *parent) : PageBase(parent)
{
    auto *page = new QVBoxLayout(this);
    page->setContentsMargins(28, 24, 28, 12);
    page->setSpacing(14);
    header(page, "settings.title", "settings.subtitle");

    auto *c1 = card(page, "settings.cardUi");
    auto *f = new QFormLayout;
    f->setHorizontalSpacing(16);
    f->setVerticalSpacing(10);
    lang_ = new QComboBox;
    lang_->addItem(QStringLiteral("Русский"), "ru");
    lang_->addItem(QStringLiteral("English"), "en");
    theme_ = new QComboBox;
    fillCombo(theme_, {{"dark", "settings.theme.dark"}, {"light", "settings.theme.light"}});
    lang_->setCurrentIndex(I18n::instance().language() == "ru" ? 0 : 1);
    theme_->setCurrentIndex(AppPaths::settings().value("ui/theme", "dark").toString() == "light" ? 1 : 0);
    f->addRow(R(new QLabel, "settings.language"), lang_);
    f->addRow(R(new QLabel, "settings.theme"), theme_);
    c1->addLayout(f);

    auto *cC = card(page, "settings.cardConsole");
    auto *fC = new QFormLayout;
    fC->setHorizontalSpacing(16);
    consoleFw_ = new QLineEdit;
    consoleFw_->setPlaceholderText("7.61");
    consoleFw_->setMaxLength(8);
    consoleFw_->setMaximumWidth(160);
    consoleFw_->setText(AppPaths::settings().value("console/fw").toString());
    fC->addRow(R(new QLabel, "settings.consoleFw"), consoleFw_);
    maxFw_ = new QLineEdit;
    maxFw_->setPlaceholderText("11.60");
    maxFw_->setMaxLength(8);
    maxFw_->setMaximumWidth(160);
    maxFw_->setText(AppPaths::settings().value("console/maxfw").toString());
    fC->addRow(R(new QLabel, "settings.maxFw"), maxFw_);
    cC->addLayout(fC);
    auto *hC = R(new QLabel, "settings.consoleFwHint");
    hC->setObjectName("muted");
    hC->setWordWrap(true);
    cC->addWidget(hC);
    connect(consoleFw_, &QLineEdit::editingFinished, this, [this] {
        AppPaths::settings().setValue("console/fw", consoleFw_->text().trimmed());
    });
    connect(maxFw_, &QLineEdit::editingFinished, this, [this] {
        AppPaths::settings().setValue("console/maxfw", maxFw_->text().trimmed());
    });

    // ---------- эмулятор AMPR ----------
    auto *cA = card(page, "settings.cardAmpr");
    amprAuto_ = R(new QCheckBox, "settings.amprAuto");
    amprAuto_->setChecked(AppPaths::settings().value("pack/amprAuto", false).toBool());
    amprForce_ = R(new QCheckBox, "settings.amprForce");
    amprForce_->setChecked(AppPaths::settings().value("pack/amprForce", false).toBool());
    amprVer_ = new QComboBox;
    for (const GameCheck::AmprBuild &b : GameCheck::amprBuilds()) amprVer_->addItem(b.version, b.version);
    amprVer_->setCurrentIndex(qMax(0, amprVer_->findData(AppPaths::settings().value("pack/amprVersion", "0.3.6.6").toString())));
    auto *fA = new QFormLayout;
    fA->setHorizontalSpacing(16);
    fA->addRow(amprAuto_);
    fA->addRow(amprForce_);
    fA->addRow(R(new QLabel, "settings.amprVersion"), amprVer_);
    cA->addLayout(fA);
    auto *hA = R(new QLabel, "settings.amprHint");
    hA->setObjectName("muted");
    hA->setWordWrap(true);
    cA->addWidget(hA);
    connect(amprForce_, &QCheckBox::toggled, this, [](bool on) { AppPaths::settings().setValue("pack/amprForce", on); });
    connect(amprAuto_, &QCheckBox::toggled, this, [](bool on) { AppPaths::settings().setValue("pack/amprAuto", on); });
    connect(amprVer_, &QComboBox::currentIndexChanged, this, [this] {
        AppPaths::settings().setValue("pack/amprVersion", amprVer_->currentData().toString());
    });

    // ---------- автозагрузка на PS5 по FTP ----------
    auto *cF = card(page, "settings.cardFtp");
    ftpEnable_ = R(new QCheckBox, "settings.ftpEnable");
    ftpEnable_->setChecked(AppPaths::settings().value("ftp/enable", true).toBool());
    cF->addWidget(ftpEnable_);
    auto *fF = new QFormLayout;
    fF->setHorizontalSpacing(16);
    ftpHost_ = new QLineEdit;
    ftpHost_->setPlaceholderText(QStringLiteral("192.168.1.50"));
    ftpHost_->setText(AppPaths::settings().value("ftp/host").toString());
    ftpPort_ = new QLineEdit;
    ftpPort_->setPlaceholderText(QStringLiteral("1337"));
    ftpPort_->setText(AppPaths::settings().value("ftp/port", "1337").toString());
    ftpPort_->setMaximumWidth(90);
    ftpPath_ = new QLineEdit;
    ftpPath_->setText(AppPaths::settings().value("ftp/path", "/data/homebrew").toString());
    ftpAnon_ = R(new QCheckBox, "settings.ftpAnon");
    ftpAnon_->setChecked(AppPaths::settings().value("ftp/anon", true).toBool());
    ftpUser_ = new QLineEdit;
    ftpUser_->setText(AppPaths::settings().value("ftp/user", "anonymous").toString());
    ftpPass_ = new QLineEdit;
    ftpPass_->setEchoMode(QLineEdit::Password);
    ftpPass_->setText(AppPaths::settings().value("ftp/pass").toString());
    fF->addRow(R(new QLabel, "settings.ftpHost"), ftpHost_);
    fF->addRow(R(new QLabel, "settings.ftpPort"), ftpPort_);
    fF->addRow(R(new QLabel, "settings.ftpPath"), ftpPath_);
    fF->addRow(ftpAnon_);
    fF->addRow(R(new QLabel, "settings.ftpUser"), ftpUser_);
    fF->addRow(R(new QLabel, "settings.ftpPass"), ftpPass_);
    cF->addLayout(fF);
    auto *hF = R(new QLabel, "settings.ftpHint");
    hF->setObjectName("muted");
    hF->setWordWrap(true);
    cF->addWidget(hF);
    auto updateFtpRows = [this, fF] {
        const bool on = ftpEnable_->isChecked();
        for (QWidget *w : {static_cast<QWidget *>(ftpHost_), static_cast<QWidget *>(ftpPort_), static_cast<QWidget *>(ftpPath_), static_cast<QWidget *>(ftpAnon_)}) w->setEnabled(on);
        const bool creds = on && !ftpAnon_->isChecked();
        ftpUser_->setEnabled(creds);
        ftpPass_->setEnabled(creds);
        fF->setRowVisible(ftpUser_, !ftpAnon_->isChecked());
        fF->setRowVisible(ftpPass_, !ftpAnon_->isChecked());
    };
    updateFtpRows();
    connect(ftpEnable_, &QCheckBox::toggled, this, [updateFtpRows](bool on) { AppPaths::settings().setValue("ftp/enable", on); updateFtpRows(); });
    connect(ftpAnon_, &QCheckBox::toggled, this, [updateFtpRows](bool on) { AppPaths::settings().setValue("ftp/anon", on); updateFtpRows(); });
    connect(ftpHost_, &QLineEdit::editingFinished, this, [this] { AppPaths::settings().setValue("ftp/host", ftpHost_->text().trimmed()); });
    connect(ftpPort_, &QLineEdit::editingFinished, this, [this] { AppPaths::settings().setValue("ftp/port", ftpPort_->text().trimmed()); });
    connect(ftpPath_, &QLineEdit::editingFinished, this, [this] { AppPaths::settings().setValue("ftp/path", ftpPath_->text().trimmed()); });
    connect(ftpUser_, &QLineEdit::editingFinished, this, [this] { AppPaths::settings().setValue("ftp/user", ftpUser_->text().trimmed()); });
    connect(ftpPass_, &QLineEdit::editingFinished, this, [this] { AppPaths::settings().setValue("ftp/pass", ftpPass_->text()); });

    auto *c2 = card(page, "settings.cardTools");
    auto *hint = R(new QLabel, "settings.toolsHint");
    hint->setObjectName("muted");
    hint->setWordWrap(true);
    c2->addWidget(hint);
    grid_ = new QGridLayout;
    grid_->setHorizontalSpacing(14);
    grid_->setVerticalSpacing(6);
    c2->addLayout(grid_);
    auto *row = new QHBoxLayout;
    auto *refresh = R(new QPushButton, "settings.refresh");
    auto *open = R(new QPushButton, "settings.openTools");
    row->addWidget(refresh);
    row->addWidget(open);
    row->addStretch(1);
    c2->addLayout(row);
    page->addStretch(1);

    connect(lang_, &QComboBox::currentIndexChanged, this, [this] {
        AppPaths::settings().setValue("ui/language", lang_->currentData().toString());
        I18n::instance().setLanguage(lang_->currentData().toString());   // -> MainWindow перерисует всё
    });
    connect(theme_, &QComboBox::currentIndexChanged, this, [this] {
        AppPaths::settings().setValue("ui/theme", code(theme_));
        Theme::apply(code(theme_));
    });
    connect(refresh, &QPushButton::clicked, this, &SettingsPage::refreshTools);
    connect(open, &QPushButton::clicked, this, [] {
        QDir().mkpath(AppPaths::toolsDir());
        QDesktopServices::openUrl(QUrl::fromLocalFile(AppPaths::toolsDir()));
    });
    refreshTools();
}

void SettingsPage::refreshTools()
{
    qDeleteAll(rows_);
    rows_.clear();
    struct T { const char *key; QString path; };
    const T tools[] = {
        {"settings.tool.mkpfs", AppPaths::mkpfsExe()},
        {"settings.tool.fpkg", AppPaths::fpkgCliExe()},
        {"settings.tool.pubcmd", AppPaths::pubCmdExe()},
        {"settings.tool.pubdll", AppPaths::pubToolsDll()},
    };
    int r = 0;
    for (const T &t : tools) {
        const bool ok = QFileInfo::exists(t.path);
        auto *name = new QLabel(I18n::instance().t(QString::fromLatin1(t.key)));
        auto *st = new QLabel(ok ? TR("settings.found") : TR("settings.missing"));
        st->setObjectName(ok ? "ok" : "warn");
        auto *p = new QLabel(QDir::toNativeSeparators(t.path));
        p->setObjectName("muted");
        p->setTextInteractionFlags(Qt::TextSelectableByMouse);
        grid_->addWidget(name, r, 0);
        grid_->addWidget(st, r, 1);
        grid_->addWidget(p, r, 2);
        rows_ << name << st << p;
        ++r;
    }
    grid_->setColumnStretch(2, 1);
}
