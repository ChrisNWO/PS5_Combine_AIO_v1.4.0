#include "AboutPage.h"

#include <QCoreApplication>

AboutPage::AboutPage(QWidget *parent) : PageBase(parent)
{
    auto *page = new QVBoxLayout(this);
    page->setContentsMargins(28, 24, 28, 12);
    page->setSpacing(14);
    header(page, "about.title", "about.subtitle");
    view_ = new QTextBrowser;
    view_->setObjectName("about");
    view_->setOpenExternalLinks(true);
    page->addWidget(view_, 1);
    fill();
}

void AboutPage::fill()
{
    auto &i = I18n::instance();
    const QString html = QStringLiteral(
        "<h2>PS5 Combine AIO <span style='font-weight:normal'>v%1</span></h2>"
        "<p>%2</p>"
        "<h3>%3</h3>"
        "<p><b>ChrisNWO</b> — %4</p>"
        "<h3>%5</h3>"
        "<ul>"
        "<li><b>MkPFS</b> — PSBrew (GPL-3.0). %6 · <a href='https://github.com/PSBrew/MkPFS'>github.com/PSBrew/MkPFS</a></li>"
        "<li><b>PSVIETHOA FPKG Builder / fpkg-cli</b> — Nguyễn Thanh Sơn &amp; Ngô Phi Phương. %7</li>"
        "<li><b>LibProsperoPkg</b> — Drakmor &amp; SvenGDK. %8 · <a href='https://github.com/SvenGDK/LibProsperoPkg'>github.com/SvenGDK/LibProsperoPkg</a></li>"
        "<li><b>PS5 PKG Tool</b> — pearlxcore (GPL-3.0). %14 · <a href='https://github.com/pearlxcore/PS5PKGTool'>github.com/pearlxcore/PS5PKGTool</a></li>"
        "<li><b>Sony Publishing Tools</b> (prospero-pub-cmd, libScePubTools) — © Sony Interactive Entertainment. %9</li>"
        "<li><b>Qt 6</b> — The Qt Company, LGPLv3.</li>"
        "</ul>"
        "<h3>%10</h3><p>%11</p>"
        "<h3>%12</h3><p>%13</p>")
        .arg(QCoreApplication::applicationVersion(),
             i.t("about.desc"), i.t("about.authorHead"), i.t("about.authorRole"),
             i.t("about.sourcesHead"), i.t("about.mkpfsRole"), i.t("about.psvRole"))
        .arg(i.t("about.drakRole"), i.t("about.sonyRole"),
             i.t("about.licenseHead"), i.t("about.license"),
             i.t("about.noticeHead"), i.t("about.notice"))
        .arg(i.t("about.pkgtoolRole"));
    view_->setHtml(html);
}
