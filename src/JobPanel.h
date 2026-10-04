#pragma once
#include <QElapsedTimer>
#include <QLabel>
#include <QList>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QStringList>
#include <QTimer>
#include <QWidget>

class ToolRunner;

struct JobSpec {
    QString title;
    QString program;
    QStringList args;
    QString workDir;
    int weight = 1;        // доля этапа в общем прогресс-баре (для цепочек)
};

// Задача очереди = цепочка из одного или нескольких запусков утилит.
struct TaskInfo {
    enum State { Queued, Running, Done, Failed, Cancelled, Interrupted };
    qint64 id = 0;
    QString title;
    QList<JobSpec> jobs;
    QString openPath;
    State state = Queued;
    QString error;
    qint64 finishedAt = 0;   // секунды с 1970
};

// Нижняя панель: общий прогресс-бар, статус, лог, «Отмена», «Открыть папку» + очередь задач с сохранением между запусками.
class JobPanel : public QWidget
{
    Q_OBJECT
public:
    explicit JobPanel(QWidget *parent = nullptr);
    void runSequence(const QList<JobSpec> &jobs, const QString &openPathOnSuccess = QString());
    bool busy() const { return busy_; }
    void retranslate();

    // очередь
    const QList<TaskInfo> &tasks() const { return tasks_; }
    bool autoStart() const { return autoStart_; }
    void setAutoStart(bool on);
    void startQueue();                    // ручной запуск (в т.ч. после паузы / перезапуска программы)
    void retry(qint64 id);
    void removeTask(qint64 id);
    void clearFinished();
    int queuedCount() const;

public slots:
    void stopAll();      // остановить текущую задачу и дождаться гибели процесса (закрытие окна / выход)
signals:
    void busyChanged(bool busy);
    void tasksChanged();
private:
    void startTask(int index);
    void startNext();
    void finishTask(TaskInfo::State state, const QString &error);
    void startStage(int i);
    void fail(const QString &why, bool cancelled);
    void setBusy(bool b);
    void repolishStatus();
    void updateElapsed();
    void onLine(const QString &line, bool fromStderr);
    void save() const;
    void load();
    int indexOf(qint64 id) const;

    ToolRunner *runner_;
    QProgressBar *bar_;
    QLabel *status_, *elapsed_;
    QPlainTextEdit *log_;
    QPushButton *cancel_, *clear_, *open_;
    QList<TaskInfo> tasks_;
    QList<JobSpec> jobs_;
    QString openPath_;
    qint64 curId_ = 0;
    int idx_ = 0, total_ = 1, done_ = 0;
    bool busy_ = false, closing_ = false, autoStart_ = true, paused_ = false;
    QElapsedTimer clock_, lastOutput_;
    QTimer tick_;
};
