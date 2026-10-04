#pragma once
#include <QAtomicInt>
#include <QByteArray>
#include <QFileInfo>
#include <QMetaType>
#include <QString>
#include <QStringList>
#include <QThread>

struct LibraryItem {
    QString title, titleId, contentId, platform, region, source, version, requiredFw, fileName, path;
    QString role;           // base | update | update-old | dlc | app | unknown
    QString sdk, sourceDetail, drm;
    QByteArray gameJson;    // полное описание Ps5GameInfo от ps5aio-helper (для трофеев, файлов, извлечения)
    qint64 size = 0;
    bool isDump = false;
};
Q_DECLARE_METATYPE(LibraryItem)

// Фоновое сканирование папок библиотеки: дампы (папки с sce_sys/param.json) и файлы .pkg/.ffpfsc/.ffpkg/.exfat/.ffpfs.
// UI не блокируется: найденные элементы приходят сигналами.
class LibraryScanner : public QThread
{
    Q_OBJECT
public:
    explicit LibraryScanner(const QStringList &roots, QObject *parent = nullptr)
        : QThread(parent), roots_(roots) {}
    void cancel() { cancel_.storeRelaxed(1); }

signals:
    void itemFound(const LibraryItem &item);
    void scanning(const QString &dir);

protected:
    void run() override;

private:
    void scanDir(const QString &dir, int depth);
    void addDump(const QString &dir);
    void addFile(const QFileInfo &fi);
    qint64 dirSize(const QString &dir);

    QStringList roots_;
    QAtomicInt cancel_;
};
