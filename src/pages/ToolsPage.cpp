#include "ToolsPage.h"

#include <QFormLayout>
#include <QMessageBox>

#include "AppPaths.h"
#include "EditFilesDialog.h"

ToolsPage::ToolsPage(QWidget *parent) : PageBase(parent)
{
    auto *page = new QVBoxLayout(this);
    page->setContentsMargins(28, 24, 28, 12);
    page->setSpacing(14);
    header(page, "tools.title", "tools.subtitle");

    auto *c1 = card(page, "tools.cardFile");
    auto *f = new QFormLayout;
    f->setHorizontalSpacing(16);
    f->setVerticalSpacing(10);
    file_ = new PathRow(PathRow::OpenFile);
    out_ = new PathRow(PathRow::OpenDir);
    passcode_ = new QLineEdit;
    passcode_->setMaxLength(32);
    f->addRow(R(new QLabel, "tools.file"), file_);
    f->addRow(R(new QLabel, "tools.outDir"), out_);
    f->addRow(R(new QLabel, "tools.passcode"), passcode_);
    c1->addLayout(f);
    kind_ = new QLabel;
    kind_->setObjectName("muted");
    c1->addWidget(kind_);

    auto *c2 = card(page, "tools.cardActions");
    full_ = R(new QCheckBox, "tools.fullVerify");
    overwrite_ = R(new QCheckBox, "tools.overwrite");
    c2->addWidget(full_);
    c2->addWidget(overwrite_);
    auto *row = new QHBoxLayout;
    bInfo_ = R(new QPushButton, "tools.info");
    bList_ = R(new QPushButton, "tools.list");
    bVerify_ = R(new QPushButton, "tools.verify");
    bExtract_ = R(new QPushButton, "tools.extract");
    bExtract_->setObjectName("primary");
    row->addWidget(bInfo_);
    row->addWidget(bList_);
    row->addWidget(bVerify_);
    row->addStretch(1);
    row->addWidget(bExtract_);
    c2->addLayout(row);

    // ---------- обслуживание образов (ps5aio-helper на базе PS5 PKG Tool) ----------
    auto *c3 = card(page, "tools.cardMaint");
    auto *mhint = R(new QLabel, "tools.maintHint");
    mhint->setObjectName("muted");
    mhint->setWordWrap(true);
    c3->addWidget(mhint);
    auto *mrow = new QHBoxLayout;
    bRepair_ = R(new QPushButton, "tools.repair");
    bAmpr_ = R(new QPushButton, "tools.ampr");
    bEdit_ = R(new QPushButton, "tools.edit");
    bVerifyImg_ = R(new QPushButton, "tools.verifyImg");
    bRebuild_ = R(new QPushButton, "tools.rebuild");
    mrow->addWidget(bVerifyImg_);
    mrow->addWidget(bRepair_);
    mrow->addWidget(bAmpr_);
    mrow->addWidget(bEdit_);
    mrow->addWidget(bRebuild_);
    mrow->addStretch(1);
    c3->addLayout(mrow);
    auto *crow = new QHBoxLayout;
    convTarget_ = new QComboBox;
    fillCombo(convTarget_, {{"exfat", "tools.conv.exfat"}, {"ffpkg", "tools.conv.ffpkg"}, {"ffpfsc", "tools.conv.ffpfsc"}});
    convOut_ = new PathRow(PathRow::SaveFile);
    bConvert_ = R(new QPushButton, "tools.convert");
    crow->addWidget(R(new QLabel, "tools.convertTo"));
    crow->addWidget(convTarget_);
    crow->addWidget(convOut_, 1);
    crow->addWidget(bConvert_);
    c3->addLayout(crow);
    page->addStretch(1);

    connect(file_, &PathRow::textChanged, this, [this] { onRetranslate(); });
    connect(bInfo_, &QPushButton::clicked, this, [this] { run(Info); });
    connect(bList_, &QPushButton::clicked, this, [this] { run(List); });
    connect(bVerify_, &QPushButton::clicked, this, [this] { run(Verify); });
    connect(bExtract_, &QPushButton::clicked, this, [this] { run(Extract); });
    connect(bRepair_, &QPushButton::clicked, this, [this] { runMaint(Repair); });
    connect(bAmpr_, &QPushButton::clicked, this, [this] { runMaint(Ampr); });
    connect(bEdit_, &QPushButton::clicked, this, [this] { runMaint(Edit); });
    connect(bVerifyImg_, &QPushButton::clicked, this, [this] { runMaint(VerifyImg); });
    connect(bRebuild_, &QPushButton::clicked, this, [this] { runMaint(Rebuild); });
    connect(bConvert_, &QPushButton::clicked, this, [this] { runMaint(Convert); });
    onRetranslate();
}

bool ToolsPage::isPkg() const { return file_->text().endsWith(QStringLiteral(".pkg"), Qt::CaseInsensitive); }

void ToolsPage::onRetranslate()
{
    file_->setFilter(TR("tools.filter"));
    file_->retranslate();
    out_->retranslate();
    convOut_->setFilter(TR("tools.convFilter"));
    convOut_->retranslate();
    const bool any = !file_->text().isEmpty();
    kind_->setText(!any ? QString() : I18n::instance().t(QString::fromLatin1(isPkg() ? "tools.kind.pkg" : "tools.kind.image")));
    passcode_->setEnabled(isPkg());
    overwrite_->setEnabled(!isPkg());
}

void ToolsPage::applyBusy()
{
    for (QPushButton *b : {bInfo_, bList_, bVerify_, bExtract_}) b->setEnabled(!busy_);
}

void ToolsPage::run(Action a)
{
    const QString f = file_->text();
    if (f.isEmpty() || !QFileInfo::exists(f)) { QMessageBox::warning(this, TR("app.name"), TR("err.noSource")); return; }
    const QString o = out_->text();
    if (a == Extract && o.isEmpty()) { QMessageBox::warning(this, TR("app.name"), TR("err.noOutput")); return; }

    QStringList args;
    QString prog, title, openPath;
    if (isPkg()) {
        prog = AppPaths::fpkgCliExe();
        const QString pc = passcode_->text().trimmed();
        switch (a) {
        case Info:    args << "pkg-info" << f; break;
        case List:    args << "pkg-list" << f; break;
        case Verify:  args << "verify" << f; if (full_->isChecked()) args << "--full"; break;
        case Extract: args << "pkg-extract" << f << "--output" << o; openPath = o; break;
        }
        if (!pc.isEmpty() && a != Verify) args << "--passcode" << pc;
        if (!pc.isEmpty() && a == Verify) args << "--passcode" << pc;
    } else {
        prog = AppPaths::mkpfsExe();
        switch (a) {
        case Info:    args << "inspect" << f; break;
        case List:    args << "tree" << f; break;
        case Verify:  args << "verify" << f; break;
        case Extract: args << "unpack" << "--deep" << f << o; if (overwrite_->isChecked()) args << "--overwrite"; openPath = o; break;
        }
    }
    if (a == Extract) QDir().mkpath(o);
    static const char *keys[] = {"tools.job.info", "tools.job.list", "tools.job.verify", "tools.job.extract"};
    title = I18n::instance().t(QString::fromLatin1(keys[a]));
    emit runRequested({JobSpec{title, prog, args, QString(), 1}}, openPath);
}

// Проверка / ремонт / правка / конвертация образов — через ps5aio-helper (библиотеки PS5 PKG Tool).
// Формат образа (exFAT / FFPKG / FFPFSC) помощник определяет по содержимому и сам сообщает, если операция не подходит.
void ToolsPage::runMaint(Maint m)
{
    if (!AppPaths::helperAvailable()) {
        QMessageBox::information(this, TR("app.name"), TR("tools.needHelper"));
        return;
    }
    const QString f = file_->text();
    if (f.isEmpty() || !QFileInfo::exists(f)) { QMessageBox::warning(this, TR("app.name"), TR("err.noSource")); return; }

    QStringList a;
    QString title;
    switch (m) {
    case VerifyImg:
        a << "verify" << f;
        title = TR("tools.job.verifyImg");
        break;
    case Repair:
    case Ampr:
    case Edit:
    case Rebuild: {
        // Эти операции меняют образ на месте (с транзакцией и проверкой), но копию лучше держать.
        if (QMessageBox::question(this, TR("app.name"), TR("tools.confirmInPlace")) != QMessageBox::Yes) return;
        if (m == Repair) { a << "repair" << f; title = TR("tools.job.repair"); }
        else if (m == Ampr) { a << "refresh-ampr" << f; title = TR("tools.job.ampr"); }
        else if (m == Rebuild) { a << "rebuild" << f; title = TR("tools.job.rebuild"); }
        else {
            EditFilesDialog dlg(this);
            if (dlg.exec() != QDialog::Accepted || dlg.operationCount() == 0) return;
            a << "edit" << f << dlg.helperArgs();
            title = TR("tools.job.edit");
        }
        break;
    }
    case Convert: {
        const QString o = convOut_->text();
        if (o.isEmpty()) { QMessageBox::warning(this, TR("app.name"), TR("err.noOutput")); return; }
        a << "convert" << f << o << "--to" << code(convTarget_);
        if (overwrite_->isChecked()) a << "--overwrite";
        emit runRequested({JobSpec{TR("tools.job.convert"), AppPaths::helperExe(), a, QString(), 1}}, QFileInfo(o).absolutePath());
        return;
    }
    }
    emit runRequested({JobSpec{title, AppPaths::helperExe(), a, QString(), 1}}, QFileInfo(f).absolutePath());
}
