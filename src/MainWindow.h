#pragma once
#include <QListWidget>
#include <QScrollArea>
#include <QMainWindow>
#include <QStackedWidget>

class JobPanel;
class PageBase;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
protected:
    void closeEvent(QCloseEvent *e) override;
private:
    void retranslate();
    void addPage(PageBase *page, const char *navKey);

    QListWidget *nav_;
    QStackedWidget *stack_;
    JobPanel *jobs_;
    class QLabel *ver_, *logoSub_;
    QList<PageBase *> pages_;
    QList<const char *> navKeys_;
};
