#include <QApplication>
#include <QIcon>
#include <QLocale>

#include "AppPaths.h"
#include "I18n.h"
#include "MainWindow.h"
#include "Theme.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("PS5 Combine AIO"));
    app.setApplicationVersion(QStringLiteral(APP_VERSION));
    app.setOrganizationName(QStringLiteral("ChrisNWO"));
    app.setWindowIcon(QIcon(QStringLiteral(":/icons/app.png")));

    QSettings &s = AppPaths::settings();
    QString lang = s.value("ui/language").toString();
    if (lang.isEmpty()) {
        const auto sys = QLocale::system().language();
        lang = (sys == QLocale::Russian || sys == QLocale::Ukrainian) ? "ru" : "en";
    }
    I18n::instance().setLanguage(lang);
    Theme::apply(s.value("ui/theme", "dark").toString());

    MainWindow w;
    w.show();
    return app.exec();
}
