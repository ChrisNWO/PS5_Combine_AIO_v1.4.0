#pragma once
#include <QComboBox>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QList>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QShowEvent>
#include <QStandardItemModel>
#include <QTabWidget>
#include <QTableView>
#include <QTreeView>
#include <QTableWidget>
#include <QTimer>

#include "LibraryScanner.h"
#include "PageBase.h"

class LibraryProxy;
class QSortFilterProxyModel;
class ToolRunner;

// «Библиотека»: дампы, .pkg и образы с поиском, фильтрами, группировкой, ролями (база/патч/DLC),
// проверкой прошивки и вкладками: обложки, трофеи, Activities/UDS, файлы внутри образа, SELF, param.json.
// Метаданные из-под образов и пакетов читает ps5aio-helper (на базе библиотек PS5 PKG Tool); без него — базовый режим.
class LibraryPage : public PageBase
{
    Q_OBJECT
public:
    explicit LibraryPage(QWidget *parent = nullptr);
    ~LibraryPage() override;

signals:
    void openIn(int target, const QString &path);   // 0 = упаковка, 1 = FPKG, 2 = анализ

protected:
    void onRetranslate() override;
    void showEvent(QShowEvent *e) override;

private:
    enum Col { CTitle, CTitleId, CContentId, CRole, CPlatform, CRegion, CSource, CSize, CVersion, CFw, CFile, CPath, CCount };
    enum Tab { TOverview, TArtwork, TTrophies, TActivities, TFiles, TExe, TRaw, TCount };

    // сканирование
    void startScan();
    void startNativeScan(const QStringList &roots);
    void startHelperScan(const QStringList &roots);
    void stopScans();
    void finishScan();
    void onHelperLine(const QString &line);
    void addItem(const LibraryItem &it);
    void addFolder();
    void removeFolder();
    void applyFilter();
    void rebuildModel();                       // плоский список или дерево по группам
    QList<QStandardItem *> makeRow(int idx) const;
    QString groupKeyFor(const QString &mode, const LibraryItem &it) const;
    QString groupLabel(const QString &mode, const QList<int> &members) const;
    QList<int> visibleIndexes() const;         // индексы items_ в порядке отображения (с учётом фильтра)
    void findDuplicates();
    void renameItems();
    void exportList();
    void compareImages();
    void rebuildFilters();
    void refreshCompat();
    void refreshRoles();
    void styleFw(QStandardItem *cell, const LibraryItem &it) const;
    struct FwVerdict { int effMin = -1; QString effText, message; QString color; };   // color пуст = всё в порядке
    FwVerdict fwVerdict(const LibraryItem &it) const;

    // выбор элемента и подробности
    void onSelection();
    void showOverview(const LibraryItem *it);
    void loadDetails(const LibraryItem &it);
    void applyDetails(const QJsonObject &d, const QString &dir);
    void clearDetails(const QString &message);
    void loadFiles(const LibraryItem &it);
    void applyFiles(const QString &jsonPath);
    void extractSelectedFiles();
    void showMenu(const QPoint &pos);
    const LibraryItem *currentItem() const;
    QString sourceText(const LibraryItem &it) const;
    QString sourceCodeText(const QString &code) const;
    QString regionText(const QString &code) const;
    QString roleText(const QString &role) const;
    QString fmtSize(qint64 bytes) const;
    QStringList folders() const;
    QString tempBase() const;
    QString tempBaseFor(const QString &path) const;

    QPushButton *bAdd_, *bRemove_, *bRescan_;
    QLabel *foldersLbl_, *countLbl_, *status_, *cover_, *detail_, *detailStatus_;
    QLineEdit *search_;
    QComboBox *srcFilter_, *regFilter_, *groupBy_;
    QTreeView *table_;
    QPushButton *bDup_, *bRename_, *bExport_, *bExpand_, *bCollapse_, *bCompare_;
    QList<QList<QStandardItem *>> rowCells_;   // ячейки строки для каждого элемента items_ (до следующей перестройки модели)
    QStandardItemModel *model_;
    LibraryProxy *proxy_;
    LibraryScanner *scanner_ = nullptr;       // нативный сканер (запасной режим)
    ToolRunner *helperScan_ = nullptr;        // сканирование через ps5aio-helper
    QStringList scanErrors_;
    QList<LibraryItem> items_;
    bool firstShow_ = true;

    // вкладки
    QTabWidget *tabs_, *udsTabs_;
    QLabel *art_[4];
    QLabel *trophyInfo_, *udsInfo_, *filesInfo_;
    QTableWidget *trophyTable_, *evTable_, *statTable_, *enumTable_, *ruleTable_;
    QPlainTextEdit *exeView_, *rawView_;
    QLineEdit *filesFilter_;
    QTableView *filesView_;
    QStandardItemModel *filesModel_;
    QSortFilterProxyModel *filesProxy_;
    QPushButton *bExtract_;
    QTimer detailTimer_;
    ToolRunner *detailRunner_ = nullptr, *filesRunner_ = nullptr;
    QHash<QString, QString> detailCache_;     // путь элемента -> папка с details.json и картинками
    QString loadedFilesFor_;                  // путь элемента, для которого загружен список файлов
    QString currentGameFile_;                 // game.json текущего элемента (для извлечения)
};
