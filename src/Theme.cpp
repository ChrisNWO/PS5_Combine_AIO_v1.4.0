#include "Theme.h"

#include <QApplication>
#include <QDir>
#include <QPainter>
#include <QPen>
#include <QPixmap>
#include <QStandardPaths>
#include <QFont>
#include <QStyleFactory>

namespace Theme {

namespace {

// Галочку для QSS (image: url(...)) рисуем сами: файлов-ресурсов у проекта нет. Два размера: обычный и @2x для HiDPI.
QString checkMarkPath()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::TempLocation) + QStringLiteral("/PS5CombineAIO");
    QDir().mkpath(dir);
    const QString base = dir + QStringLiteral("/check.png");
    for (const int scale : {1, 2}) {
        const int px = 18 * scale;
        QPixmap pm(px, px);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing, true);
        QPen pen(Qt::white, 2.6 * scale, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        p.setPen(pen);
        const QPointF pts[3] = {{4.6 * scale, 9.4 * scale}, {7.7 * scale, 12.5 * scale}, {13.4 * scale, 5.6 * scale}};
        p.drawPolyline(pts, 3);
        p.end();
        pm.save(scale == 1 ? base : dir + QStringLiteral("/check@2x.png"), "PNG");
    }
    return base;
}

} // namespace

void apply(const QString &name)
{
    const bool dark = (name != QLatin1String("light"));
    struct C { const char *k; const char *dark; const char *light; };
    static const C colors[] = {
        {"@BG@",     "#14161b", "#f3f5f9"}, {"@PANEL@",  "#1a1d24", "#ffffff"},
        {"@CARD@",   "#20242d", "#ffffff"}, {"@BORDER@", "#2d323e", "#d8dee8"},
        {"@TEXT@",   "#e7eaf0", "#1c2230"}, {"@MUTED@",  "#98a2b3", "#667085"},
        {"@ACCENT@", "#5b8cff", "#3b6cf6"}, {"@ACCENT2@","#7ba2ff", "#2f58d4"},
        {"@INPUT@",  "#171a20", "#f7f9fc"}, {"@OK@",     "#3ecf8e", "#12a26b"},
        {"@ERR@",    "#ff6b6b", "#d92d20"}, {"@SEL@",    "#2a3550", "#e3ebff"},
    };

    QString qss = QStringLiteral(R"(
* { font-family: "Segoe UI", "Inter", "Noto Sans", sans-serif; font-size: 10pt; }
QWidget { background: @BG@; color: @TEXT@; }
QFrame#sidebar { background: @PANEL@; border-right: 1px solid @BORDER@; }
QLabel#logo { font-size: 15pt; font-weight: 700; padding: 18px 16px 2px 16px; background: transparent; }
QLabel#logoSub { color: @MUTED@; padding: 0 16px 14px 16px; background: transparent; }
QLabel#ver { color: @MUTED@; padding: 10px 16px; background: transparent; }
QListWidget#nav { background: transparent; border: none; outline: 0; padding: 4px 8px; }
QListWidget#nav::item { padding: 11px 12px; margin: 2px 0; border-radius: 8px; color: @MUTED@; }
QListWidget#nav::item:hover { background: @CARD@; color: @TEXT@; }
QListWidget#nav::item:selected { background: @SEL@; color: @ACCENT@; font-weight: 600; }
QLabel#h1 { font-size: 19pt; font-weight: 700; background: transparent; }
QLabel#sub { color: @MUTED@; background: transparent; }
QLabel#cardTitle { font-size: 11pt; font-weight: 600; background: transparent; }
QLabel#muted { color: @MUTED@; background: transparent; }
QLabel#warn { color: @ERR@; background: transparent; }
QLabel#ok { color: @OK@; background: transparent; }
QLabel { background: transparent; }
QCheckBox { background: transparent; spacing: 10px; }
QCheckBox::indicator, QAbstractItemView::indicator { width: 18px; height: 18px; border: 2px solid @MUTED@; border-radius: 5px; background: @INPUT@; }
QCheckBox::indicator:hover, QAbstractItemView::indicator:hover { border-color: @ACCENT@; }
QCheckBox::indicator:checked, QAbstractItemView::indicator:checked { background: @ACCENT@; border-color: @ACCENT@; image: url("@CHECK@"); }
QCheckBox::indicator:disabled, QAbstractItemView::indicator:disabled { border-color: @BORDER@; background: transparent; }
QCheckBox::indicator:checked:disabled, QAbstractItemView::indicator:checked:disabled { background: @BORDER@; border-color: @BORDER@; }
QCheckBox:disabled { color: @MUTED@; }
QFrame#card { background: @CARD@; border: 1px solid @BORDER@; border-radius: 12px; }
QFrame#card QLabel, QFrame#card QCheckBox { background: transparent; }
QLineEdit, QComboBox, QSpinBox { background: @INPUT@; border: 1px solid @BORDER@; border-radius: 8px; padding: 7px 10px; selection-background-color: @ACCENT@; }
QLineEdit:focus, QComboBox:focus, QSpinBox:focus { border: 1px solid @ACCENT@; }
QComboBox::drop-down { border: none; width: 24px; }
QComboBox QAbstractItemView { background: @CARD@; border: 1px solid @BORDER@; selection-background-color: @SEL@; selection-color: @TEXT@; }
QPushButton { background: @CARD@; border: 1px solid @BORDER@; border-radius: 8px; padding: 8px 16px; }
QPushButton:hover { border-color: @ACCENT@; }
QPushButton:disabled { color: @MUTED@; border-color: @BORDER@; }
QPushButton#primary { background: @ACCENT@; border: none; color: white; font-weight: 600; padding: 10px 26px; }
QPushButton#primary:hover { background: @ACCENT2@; }
QPushButton#primary:disabled { background: @BORDER@; color: @MUTED@; }
QProgressBar { background: @INPUT@; border: 1px solid @BORDER@; border-radius: 8px; height: 18px; text-align: center; color: @TEXT@; }
QProgressBar::chunk { background: @ACCENT@; border-radius: 7px; }
QPlainTextEdit, QTextBrowser { background: @INPUT@; border: 1px solid @BORDER@; border-radius: 10px; padding: 6px; font-family: "Cascadia Mono", "JetBrains Mono", Consolas, monospace; font-size: 9pt; }
QTextBrowser#about { font-family: "Segoe UI", "Inter", sans-serif; font-size: 10pt; }
QTableView { background: @INPUT@; border: 1px solid @BORDER@; border-radius: 10px; gridline-color: @BORDER@; selection-background-color: @SEL@; selection-color: @TEXT@; outline: 0; }
QTableView::item { padding: 4px 8px; border: none; }
QHeaderView { background: transparent; }
QHeaderView::section { background: @CARD@; color: @MUTED@; border: none; border-bottom: 1px solid @BORDER@; padding: 8px 10px; font-weight: 600; }
QTableCornerButton::section { background: @CARD@; border: none; }
QMenu { background: @CARD@; border: 1px solid @BORDER@; border-radius: 8px; padding: 6px; }
QMenu::item { padding: 7px 22px; border-radius: 6px; }
QMenu::item:selected { background: @SEL@; }
QMenu::separator { height: 1px; background: @BORDER@; margin: 5px 8px; }
QSplitter::handle { background: @BORDER@; height: 2px; }
QScrollArea { border: none; background: transparent; }
QScrollBar:vertical { background: transparent; width: 10px; margin: 2px; }
QScrollBar::handle:vertical { background: @BORDER@; border-radius: 4px; min-height: 30px; }
QScrollBar::add-line, QScrollBar::sub-line { height: 0; width: 0; }
QToolTip { background: @CARD@; color: @TEXT@; border: 1px solid @BORDER@; }
)");
    for (const C &c : colors)
        qss.replace(QLatin1String(c.k), QLatin1String(dark ? c.dark : c.light));
    qss.replace(QLatin1String("@CHECK@"), checkMarkPath());       // QSS ждёт прямые слэши

    qApp->setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    qApp->setStyleSheet(qss);
}

} // namespace Theme
