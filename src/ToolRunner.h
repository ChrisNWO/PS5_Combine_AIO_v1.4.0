#pragma once
// ToolRunner — асинхронный запуск консольной утилиты (mkpfs.exe, prospero-pub-cmd.exe, ...)
// с разбором stdout/stderr и трансляцией процента выполнения в GUI.
// Qt 6, C++17. Отдельный поток НЕ нужен: QProcess и так работает через event loop.

#include <QObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QStringList>
#include <QTimer>

class ToolRunner : public QObject
{
    Q_OBJECT
public:
    explicit ToolRunner(QObject *parent = nullptr);
    ~ToolRunner() override;

    // Регулярка с ОДНОЙ группой захвата — число 0..100. По умолчанию ловит "] 45%":
    // так выглядят прогресс-строки и mkpfs (stderr, \r), и fpkg-cli.
    void setProgressPattern(const QRegularExpression &re);

    // Отображение 0..100 утилиты на диапазон общего прогресс-бара.
    // Нужно для цепочек: mkpfs -> 0..60, pkg-tool -> 60..100.
    void setProgressRange(int from, int to);

    // Если утилита молчит дольше N мс — считаем зависшей и убиваем. 0 = выключено.
    void setIdleTimeout(int ms);

    // Доп. переменные окружения (накладываются поверх системных).
    void setExtraEnvironment(const QProcessEnvironment &env);

    bool isRunning() const;
    bool wasCancelled() const { return cancelled_; }

    // Жёстко остановить утилиту вместе со всеми её дочерними процессами и дождаться завершения.
    // Возвращает true, если процесса больше нет. Безопасно вызывать повторно.
    bool stopAndWait(int timeoutMs = 8000);

public slots:
    void start(const QString &program, const QStringList &args,
               const QString &workDir = QString());
    void cancel();

signals:
    void progress(int percent);                       // уже отмасштабирован setProgressRange
    void logLine(const QString &line, bool fromStderr);
    void finished(bool ok, int exitCode, const QString &errorText);

private:
    void readChannel(QProcess::ProcessChannel ch);
    void consume(QByteArray &carry, const QByteArray &chunk, bool isErr);
    void handleLine(const QByteArray &raw, bool isErr);
    void flushCarry();
    void onFinished(int code, QProcess::ExitStatus st);
    void onError(QProcess::ProcessError e);
    void killTree();
    void resetJob();
    void closeJob();
    void attachToJob();

    QProcess *proc_ = nullptr;
    QTimer idle_;
    QRegularExpression pattern_;
    QProcessEnvironment extraEnv_;
    int rangeFrom_ = 0, rangeTo_ = 100;
    int idleMs_ = 0;
    int lastPercent_ = -1;
    bool cancelled_ = false;
    bool idleKilled_ = false;
    bool startFailed_ = false;
    void *job_ = nullptr;      // Windows Job Object (KILL_ON_JOB_CLOSE): ОС сама убьёт утилиту и потомков, если GUI умрёт
    bool inJob_ = false;
    QByteArray carryOut_, carryErr_;
    QStringList stderrTail_;
};
