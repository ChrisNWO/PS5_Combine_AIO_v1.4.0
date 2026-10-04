#include "ToolRunner.h"

#include <QStringDecoder>
#include <algorithm>

#ifdef Q_OS_WIN
#  include <qt_windows.h>
#endif

#ifdef Q_OS_WIN
namespace {
// Job Object с флагом KILL_ON_JOB_CLOSE: когда последний дескриптор закрывается (в т.ч. при аварийном завершении
// или снятии задачи через Диспетчер задач), Windows убивает ВСЕ процессы в задании. Потомки утилиты попадают туда автоматически.
HANDLE createKillOnCloseJob()
{
    HANDLE h = CreateJobObjectW(nullptr, nullptr);
    if (!h)
        return nullptr;
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION info;
    ZeroMemory(&info, sizeof(info));
    info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!SetInformationJobObject(h, JobObjectExtendedLimitInformation, &info, sizeof(info))) {
        CloseHandle(h);
        return nullptr;
    }
    return h;
}
} // namespace
#endif

namespace {

QString decode(const QByteArray &b)
{
    // Python/.NET-утилиты на Windows могут отдавать и UTF-8, и OEM-кодировку.
    QStringDecoder dec(QStringDecoder::Utf8);
    QString s = dec(b);
    if (dec.hasError())
        s = QString::fromLocal8Bit(b);
    return s;
}

} // namespace

ToolRunner::ToolRunner(QObject *parent)
    : QObject(parent)
    , proc_(new QProcess(this))
    , pattern_(QStringLiteral(R"(\]\s*(\d{1,3}(?:[.,]\d+)?)\s*%)"))
{
    idle_.setSingleShot(true);
    connect(&idle_, &QTimer::timeout, this, [this] {
        idleKilled_ = true;
        killTree();
    });

#ifdef Q_OS_WIN
    // Не показывать чёрное консольное окно у дочерних утилит.
    proc_->setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *a) {
        a->flags |= CREATE_NO_WINDOW;
    });
#endif

    connect(proc_, &QProcess::readyReadStandardOutput, this,
            [this] { readChannel(QProcess::StandardOutput); });
    connect(proc_, &QProcess::readyReadStandardError, this,
            [this] { readChannel(QProcess::StandardError); });
    // stdin закрываем сразу: утилита не должна ждать ввода (интерактивные "Press any key", y/n).
    connect(proc_, &QProcess::started, proc_, &QProcess::closeWriteChannel);
    connect(proc_, &QProcess::started, this, [this] { attachToJob(); });
    connect(proc_, &QProcess::errorOccurred, this, &ToolRunner::onError);
    connect(proc_, &QProcess::finished, this, &ToolRunner::onFinished);
}

ToolRunner::~ToolRunner()
{
    // Владелец (JobPanel) уже частично разрушен: наружу сигналы не отправляем, иначе обработчик полезет в мёртвые объекты.
    blockSignals(true);
    idle_.stop();
    if (isRunning()) {
        killTree();
        proc_->waitForFinished(3000);
    }
    closeJob();      // KILL_ON_JOB_CLOSE — страховка: убьёт всё, что могло остаться
}

bool ToolRunner::stopAndWait(int timeoutMs)
{
    if (!isRunning())
        return true;
    cancelled_ = true;
    idle_.stop();
    killTree();
    return proc_->waitForFinished(timeoutMs) || !isRunning();
}

void ToolRunner::closeJob()
{
#ifdef Q_OS_WIN
    if (job_) {
        CloseHandle(static_cast<HANDLE>(job_));   // при KILL_ON_JOB_CLOSE это убивает оставшиеся процессы задания
        job_ = nullptr;
    }
#endif
    inJob_ = false;
}

void ToolRunner::resetJob()
{
    closeJob();
#ifdef Q_OS_WIN
    job_ = createKillOnCloseJob();
#endif
}

// Вызывается сразу после старта процесса: помещаем его в задание. Все его будущие потомки наследуют членство.
void ToolRunner::attachToJob()
{
#ifdef Q_OS_WIN
    const qint64 pid = proc_->processId();
    if (!job_ || pid <= 0)
        return;
    HANDLE ph = OpenProcess(PROCESS_SET_QUOTA | PROCESS_TERMINATE, FALSE, DWORD(pid));
    if (!ph)
        return;
    inJob_ = AssignProcessToJobObject(static_cast<HANDLE>(job_), ph) != FALSE;
    CloseHandle(ph);
#endif
}

void ToolRunner::setProgressPattern(const QRegularExpression &re) { pattern_ = re; }

void ToolRunner::setProgressRange(int from, int to)
{
    rangeFrom_ = from;
    rangeTo_ = to;
}

void ToolRunner::setIdleTimeout(int ms) { idleMs_ = ms; }

void ToolRunner::setExtraEnvironment(const QProcessEnvironment &env) { extraEnv_ = env; }

bool ToolRunner::isRunning() const { return proc_->state() != QProcess::NotRunning; }

void ToolRunner::start(const QString &program, const QStringList &args, const QString &workDir)
{
    if (isRunning())
        return;

    cancelled_ = idleKilled_ = startFailed_ = false;
    lastPercent_ = -1;
    carryOut_.clear();
    carryErr_.clear();
    stderrTail_.clear();

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    // Без этого Python при запуске через pipe буферизует stdout блоками,
    // и прогресс приходит одним куском в самом конце.
    env.insert(QStringLiteral("PYTHONUNBUFFERED"), QStringLiteral("1"));
    env.insert(QStringLiteral("PYTHONIOENCODING"), QStringLiteral("utf-8"));
    env.insert(extraEnv_);
    proc_->setProcessEnvironment(env);

    if (!workDir.isEmpty())
        proc_->setWorkingDirectory(workDir);

    resetJob();
    proc_->setProgram(program);
    proc_->setArguments(args);
    proc_->start();

    if (idleMs_ > 0)
        idle_.start(idleMs_);
}

void ToolRunner::cancel()
{
    if (!isRunning())
        return;
    cancelled_ = true;
#ifdef Q_OS_WIN
    // terminate() у консольных приложений на Windows ничего не делает (шлёт WM_CLOSE окну, которого нет).
    killTree();
#else
    proc_->terminate();
    QTimer::singleShot(2000, this, [this] {
        if (isRunning())
            proc_->kill();
    });
#endif
}

void ToolRunner::killTree()
{
#ifdef Q_OS_WIN
    if (inJob_ && job_) {
        // Убивает утилиту и ВСЕХ её потомков разом (PyInstaller --onefile, prospero-pub-cmd и т.п.).
        TerminateJobObject(static_cast<HANDLE>(job_), 1);
    } else {
        // Запасной путь, если поместить процесс в задание не удалось.
        const qint64 pid = proc_->processId();
        if (pid > 0)
            QProcess::execute(QStringLiteral("taskkill"),
                              {QStringLiteral("/PID"), QString::number(pid),
                               QStringLiteral("/T"), QStringLiteral("/F")});
    }
#endif
    if (isRunning())
        proc_->kill();
}

void ToolRunner::readChannel(QProcess::ProcessChannel ch)
{
    if (idleMs_ > 0)
        idle_.start(idleMs_);

    if (ch == QProcess::StandardOutput)
        consume(carryOut_, proc_->readAllStandardOutput(), false);
    else
        consume(carryErr_, proc_->readAllStandardError(), true);
}

// Прогресс-бары пишут '\r' без '\n', поэтому canReadLine()/readLine() здесь не подходят:
// режем поток вручную по любому из двух разделителей.
void ToolRunner::consume(QByteArray &carry, const QByteArray &chunk, bool isErr)
{
    carry += chunk;
    qsizetype start = 0;
    for (qsizetype i = 0; i < carry.size(); ++i) {
        const char c = carry.at(i);
        if (c == '\r' || c == '\n') {
            if (i > start)
                handleLine(carry.mid(start, i - start), isErr);
            start = i + 1;
        }
    }
    carry.remove(0, start);

    // Защита от утилиты, которая льёт бесконечную строку без разделителей.
    if (carry.size() > 16 * 1024 * 1024) {      // JSON-строки с описанием игры бывают большими
        handleLine(carry, isErr);
        carry.clear();
    }
}

void ToolRunner::handleLine(const QByteArray &raw, bool isErr)
{
    const QString line = decode(raw).trimmed();
    if (line.isEmpty())
        return;

    emit logLine(line, isErr);

    if (isErr) {
        stderrTail_ << line;
        if (stderrTail_.size() > 20)
            stderrTail_.removeFirst();
    }

    const QRegularExpressionMatch m = pattern_.match(line);
    if (!m.hasMatch())
        return;

    const double pct = std::clamp(m.captured(1).replace(QLatin1Char(','), QLatin1Char('.')).toDouble(), 0.0, 100.0);
    const int mapped = rangeFrom_ + int(pct * (rangeTo_ - rangeFrom_) / 100.0 + 0.5);
    if (mapped != lastPercent_) {
        lastPercent_ = mapped;
        emit progress(mapped);
    }
}

void ToolRunner::flushCarry()
{
    if (!carryOut_.isEmpty()) { handleLine(carryOut_, false); carryOut_.clear(); }
    if (!carryErr_.isEmpty()) { handleLine(carryErr_, true);  carryErr_.clear(); }
}

void ToolRunner::onError(QProcess::ProcessError e)
{
    // Для FailedToStart сигнал finished() НЕ приходит — закрываем задачу здесь.
    if (e == QProcess::FailedToStart) {
        idle_.stop();
        startFailed_ = true;
        emit finished(false, -1,
                      tr("Не удалось запустить «%1»: %2").arg(proc_->program(), proc_->errorString()));
    }
}

void ToolRunner::onFinished(int code, QProcess::ExitStatus st)
{
    idle_.stop();
    // Дочитать хвост, который мог не успеть прийти отдельными readyRead.
    consume(carryOut_, proc_->readAllStandardOutput(), false);
    consume(carryErr_, proc_->readAllStandardError(), true);
    flushCarry();

    const bool ok = !cancelled_ && !idleKilled_ && st == QProcess::NormalExit && code == 0;

    QString err;
    if (cancelled_)
        err = tr("Отменено пользователем");
    else if (idleKilled_)
        err = tr("Утилита не выдавала вывод %1 с и была остановлена").arg(idleMs_ / 1000);
    else if (st == QProcess::CrashExit)
        err = tr("Процесс аварийно завершился");
    else if (code != 0)
        err = tr("Код выхода %1. %2").arg(code).arg(stderrTail_.join(QLatin1Char('\n')));

    if (ok && lastPercent_ != rangeTo_) {
        lastPercent_ = rangeTo_;
        emit progress(rangeTo_);   // утилита могла не напечатать финальные 100%
    }
    emit finished(ok, code, err);
}
