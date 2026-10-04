#include "MainWindow.h"

#include <QCloseEvent>
#include <QCoreApplication>
#include <QMessageBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QScrollArea>
#include <QSplitter>
#include <QVBoxLayout>

#include "AppPaths.h"
#include "I18n.h"
#include "JobPanel.h"
#include "NavIcons.h"
#include "pages/AboutPage.h"
#include "pages/BackportPage.h"
#include "pages/FpkgPage.h"
#include "pages/LibraryPage.h"
#include "pages/PackPage.h"
#include "pages/SettingsPage.h"
#include "pages/TasksPage.h"
#include "pages/ToolsPage.h"

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent)
{
    auto *root = new QWidget;
    auto *h = new QHBoxLayout(root);
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(0);

    // ---- боковая панель ----
    auto *side = new QFrame;
    side->setObjectName("sidebar");
    side->setFixedWidth(230);
    auto *sv = new QVBoxLayout(side);
    sv->setContentsMargins(0, 0, 0, 0);
    sv->setSpacing(0);
    auto *logo = new QLabel(QStringLiteral("PS5 Combine AIO"));
    logo->setObjectName("logo");
    logoSub_ = new QLabel;
    logoSub_->setObjectName("logoSub");
    nav_ = new QListWidget;
    nav_->setObjectName("nav");
    nav_->setFocusPolicy(Qt::NoFocus);
    nav_->setIconSize(QSize(22, 22));
    ver_ = new QLabel;
    ver_->setObjectName("ver");
    sv->addWidget(logo);
    sv->addWidget(logoSub_);
    sv->addWidget(nav_, 1);
    sv->addWidget(ver_);

    // ---- страницы + панель заданий ----
    stack_ = new QStackedWidget;
    jobs_ = new JobPanel;
    auto *split = new QSplitter(Qt::Vertical);
    split->addWidget(stack_);
    split->addWidget(jobs_);
    split->setStretchFactor(0, 3);
    split->setStretchFactor(1, 1);
    split->setChildrenCollapsible(false);

    h->addWidget(side);
    h->addWidget(split, 1);
    setCentralWidget(root);

    auto *lib = new LibraryPage;
    auto *pack = new PackPage;
    auto *fpkg = new FpkgPage;
    auto *tools = new ToolsPage;
    auto *backport = new BackportPage;
    addPage(lib, "nav.library");
    addPage(pack, "nav.pack");
    addPage(fpkg, "nav.fpkg");
    addPage(tools, "nav.tools");
    addPage(backport, "nav.backport");
    addPage(new TasksPage(jobs_), "nav.tasks");
    addPage(new SettingsPage, "nav.settings");
    addPage(new AboutPage, "nav.about");

    // Любой выход из программы (в т.ч. не через крестик) сначала гасит запущенную утилиту.
    connect(qApp, &QCoreApplication::aboutToQuit, jobs_, &JobPanel::stopAll);
    // «Передать в…» из контекстного меню Библиотеки: 0 = упаковка, 1 = FPKG, 2 = анализ, 3 = бэкпорт
    connect(lib, &LibraryPage::openIn, this, [this, pack, fpkg, tools, backport](int target, const QString &path) {
        PageBase *dst = target == 0 ? static_cast<PageBase *>(pack) : target == 1 ? static_cast<PageBase *>(fpkg)
                      : target == 2 ? static_cast<PageBase *>(tools) : static_cast<PageBase *>(backport);
        dst->setPath(path);
        nav_->setCurrentRow(target + 1);
    });
    connect(nav_, &QListWidget::currentRowChanged, stack_, &QStackedWidget::setCurrentIndex);
    connect(&I18n::instance(), &I18n::languageChanged, this, &MainWindow::retranslate);
    // Кнопки запуска страниц больше не блокируются во время работы: новая задача встаёт в очередь (страница «Задачи»).

    resize(1120, 780);
    setMinimumSize(980, 680);
    nav_->setCurrentRow(0);
    retranslate();
}

void MainWindow::addPage(PageBase *page, const char *navKey)
{
    // Каждая страница в прокручиваемой области — окно можно уменьшать.
    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setWidget(page);
    stack_->addWidget(scroll);
    nav_->addItem(new QListWidgetItem(NavIcons::icon(static_cast<NavIcons::Kind>(navKeys_.size())), QString()));
    pages_ << page;
    navKeys_ << navKey;
    connect(page, &PageBase::runRequested, this, [this](const QList<JobSpec> &j, const QString &open) {
        jobs_->runSequence(j, open);
    });
}

void MainWindow::retranslate()
{
    for (int i = 0; i < navKeys_.size(); ++i)
        nav_->item(i)->setText(QStringLiteral(" ") + I18n::instance().t(QString::fromLatin1(navKeys_[i])));
    for (PageBase *p : std::as_const(pages_)) p->retranslate();
    jobs_->retranslate();
    logoSub_->setText(TR("app.tagline"));
    ver_->setText(QStringLiteral("v%1 · by ChrisNWO").arg(QCoreApplication::applicationVersion()));
    setWindowTitle(QStringLiteral("PS5 Combine AIO v%1 — ChrisNWO").arg(QCoreApplication::applicationVersion()));
}

void MainWindow::closeEvent(QCloseEvent *e)
{
    if (jobs_->busy()) {
        QMessageBox box(QMessageBox::Warning, TR("app.name"), TR("close.confirm"),
                        QMessageBox::Yes | QMessageBox::No, this);
        box.setDefaultButton(QMessageBox::No);
        box.button(QMessageBox::Yes)->setText(TR("close.yes"));
        box.button(QMessageBox::No)->setText(TR("close.no"));
        if (box.exec() != QMessageBox::Yes) {
            e->ignore();
            return;
        }
        setEnabled(false);
        jobs_->stopAll();            // ждём, пока утилита и её потомки реально завершатся
    }
    e->accept();
}
