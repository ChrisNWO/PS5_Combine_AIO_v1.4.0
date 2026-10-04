#include "JobPanel.h"

#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStyle>
#include <QUrl>
#include <QVBoxLayout>

#include "AppPaths.h"
#include "ErrorHints.h"
#include "I18n.h"
#include "ToolRunner.h"

static QString storePath() { return AppPaths::dataDir() + QStringLiteral("/PS5CombineAIO.tasks.json"); }

static QString clockText(qint64 sec)
{
    return QString::asprintf("%02d:%02d", int(sec / 60), int(sec % 60));
}

// Заголовок задачи: название первого этапа + имя файла/папки, с которой он работает.
static QString makeTitle(const QList<JobSpec> &jobs)
{
    QString name;
    for (auto it = jobs.first().args.crbegin(); it != jobs.first().args.crend(); ++it) {
        if (it->startsWith(QLatin1String("--"))) continue;
        if (it->contains(QLatin1Char('\\')) || it->contains(QLatin1Char('/'))) {
            name = QFileInfo(*it).fileName();
            if (name.isEmpty()) name = *it;
            break;
        }
    }
    QString t = jobs.first().title;
    if (!name.isEmpty()) t += QStringLiteral(" · ") + name;
    if (jobs.size() > 1) t += QStringLiteral(" (+%1)").arg(jobs.size() - 1);
    return t;
}

JobPanel::JobPanel(QWidget *parent) : QWidget(parent), runner_(new ToolRunner(this))
{
    auto *v = new QVBoxLayout(this);
    v->setContentsMargins(20, 12, 20, 16);
    v->setSpacing(8);

    auto *top = new QHBoxLayout;
    status_ = new QLabel;
    status_->setObjectName("muted");
    bar_ = new QProgressBar;
    bar_->setRange(0, 100);
    cancel_ = new QPushButton;
    open_ = new QPushButton;
    clear_ = new QPushButton;
    open_->hide();
    cancel_->setEnabled(false);
    elapsed_ = new QLabel;
    elapsed_->setObjectName("muted");
    top->addWidget(status_, 1);
    top->addWidget(elapsed_);
    top->addWidget(open_);
    top->addWidget(clear_);
    top->addWidget(cancel_);

    log_ = new QPlainTextEdit;
    log_->setReadOnly(true);
    log_->setMaximumBlockCount(5000);
    log_->setMinimumHeight(90);

    v->addLayout(top);
    v->addWidget(bar_);
    v->addWidget(log_, 1);

    connect(runner_, &ToolRunner::progress, bar_, &QProgressBar::setValue);
    connect(runner_, &ToolRunner::logLine, this, &JobPanel::onLine);
    connect(runner_, &ToolRunner::finished, this, [this](bool ok, int, const QString &err) {
        if (!ok) { fail(err.isEmpty() ? TR("job.failed") : err, runner_->wasCancelled()); return; }
        done_ += qMax(1, jobs_[idx_].weight);
        if (++idx_ < jobs_.size()) { startStage(idx_); return; }
        bar_->setValue(100);
        const qint64 s = clock_.elapsed() / 1000;
        const QString msg = I18n::instance().t("job.done", {clockText(s)});
        status_->setObjectName("ok");
        repolishStatus();
        status_->setText(msg);
        log_->appendPlainText(QStringLiteral("✔ ") + msg);
        if (!openPath_.isEmpty()) open_->show();
        finishTask(TaskInfo::Done, QString());
    });
    tick_.setInterval(1000);
    connect(&tick_, &QTimer::timeout, this, &JobPanel::updateElapsed);
    connect(cancel_, &QPushButton::clicked, runner_, &ToolRunner::cancel);
    connect(clear_, &QPushButton::clicked, log_, &QPlainTextEdit::clear);
    connect(open_, &QPushButton::clicked, this, [this] {
        QDesktopServices::openUrl(QUrl::fromLocalFile(openPath_));
    });

    autoStart_ = AppPaths::settings().value("queue/autostart", true).toBool();
    load();
    paused_ = queuedCount() > 0;           // восстановленные задачи сами не стартуют — только по «Запустить очередь»
    retranslate();
    status_->setText(queuedCount() > 0 ? I18n::instance().t("job.queuedRestored", {queuedCount()}) : TR("job.idle"));
}

void JobPanel::retranslate()
{
    cancel_->setText(TR("job.cancel"));
    clear_->setText(TR("job.clear"));
    open_->setText(TR("job.openFolder"));
    updateElapsed();
    if (!busy_ && bar_->value() == 0)
        status_->setText(queuedCount() > 0 ? I18n::instance().t("job.queuedRestored", {queuedCount()}) : TR("job.idle"));
}

void JobPanel::setBusy(bool b)
{
    busy_ = b;
    cancel_->setEnabled(b);
    if (b) {                       // «часы жизни»: пользователь видит, что программа работает, даже если утилита молчит
        lastOutput_.start();
        tick_.start();
        updateElapsed();
    } else {
        tick_.stop();
        elapsed_->clear();
    }
    emit busyChanged(b);
}

// ---------------------------------------------------------------- очередь

int JobPanel::queuedCount() const
{
    int n = 0;
    for (const TaskInfo &t : tasks_) if (t.state == TaskInfo::Queued) ++n;
    return n;
}

int JobPanel::indexOf(qint64 id) const
{
    for (int i = 0; i < tasks_.size(); ++i) if (tasks_[i].id == id) return i;
    return -1;
}

void JobPanel::setAutoStart(bool on)
{
    autoStart_ = on;
    AppPaths::settings().setValue("queue/autostart", on);
    if (on && !busy_) startNext();
}

void JobPanel::runSequence(const QList<JobSpec> &jobs, const QString &openPathOnSuccess)
{
    if (jobs.isEmpty()) return;
    TaskInfo t;
    t.id = QDateTime::currentMSecsSinceEpoch();
    while (indexOf(t.id) >= 0) ++t.id;
    t.title = makeTitle(jobs);
    t.jobs = jobs;
    t.openPath = openPathOnSuccess;
    tasks_.append(t);
    save();
    emit tasksChanged();
    if (!busy_ && autoStart_) {
        // При паузе (после отмены / восстановленная очередь) новая задача стартует сама, старые ждут ручного запуска.
        if (paused_) startTask(tasks_.size() - 1);
        else startNext();
    }
    else if (busy_) updateElapsed();
    else status_->setText(I18n::instance().t("job.queuedRestored", {queuedCount()}));
}

void JobPanel::startQueue()
{
    paused_ = false;
    if (!busy_) startNext();
}

void JobPanel::startNext()
{
    if (busy_ || paused_) return;
    for (int i = 0; i < tasks_.size(); ++i)
        if (tasks_[i].state == TaskInfo::Queued) { startTask(i); return; }
}

void JobPanel::startTask(int index)
{
    TaskInfo &t = tasks_[index];
    t.state = TaskInfo::Running;
    t.error.clear();
    curId_ = t.id;
    jobs_ = t.jobs;
    openPath_ = t.openPath;
    idx_ = 0;
    done_ = 0;
    total_ = 0;
    for (const JobSpec &j : jobs_) total_ += qMax(1, j.weight);
    closing_ = false;
    bar_->setValue(0);
    open_->hide();
    status_->setObjectName("muted");
    repolishStatus();
    clock_.start();
    save();
    emit tasksChanged();
    setBusy(true);
    startStage(0);
}

void JobPanel::finishTask(TaskInfo::State state, const QString &error)
{
    const int i = indexOf(curId_);
    if (i >= 0) {
        tasks_[i].state = state;
        tasks_[i].error = error;
        tasks_[i].finishedAt = QDateTime::currentSecsSinceEpoch();
    }
    curId_ = 0;
    // история: храним не более 50 завершённых
    int finished = 0;
    for (int k = tasks_.size() - 1; k >= 0; --k) {
        const auto s = tasks_[k].state;
        if (s != TaskInfo::Queued && s != TaskInfo::Running && ++finished > 50) tasks_.removeAt(k);
    }
    save();
    setBusy(false);
    emit tasksChanged();
    if (state == TaskInfo::Cancelled) paused_ = true;           // после ручной отмены очередь не едет дальше сама
    if (autoStart_ && !paused_ && !closing_ && queuedCount() > 0)
        QTimer::singleShot(0, this, &JobPanel::startNext);
    else if (queuedCount() > 0 && !closing_)
        status_->setText(I18n::instance().t("job.queuedPaused", {queuedCount()}));
}

void JobPanel::retry(qint64 id)
{
    const int i = indexOf(id);
    if (i < 0 || tasks_[i].state == TaskInfo::Running) return;
    tasks_[i].state = TaskInfo::Queued;
    tasks_[i].error.clear();
    paused_ = false;
    save();
    emit tasksChanged();
    if (!busy_ && autoStart_) startNext();
}

void JobPanel::removeTask(qint64 id)
{
    const int i = indexOf(id);
    if (i < 0 || tasks_[i].state == TaskInfo::Running) return;
    tasks_.removeAt(i);
    save();
    emit tasksChanged();
}

void JobPanel::clearFinished()
{
    for (int k = tasks_.size() - 1; k >= 0; --k)
        if (tasks_[k].state == TaskInfo::Done) tasks_.removeAt(k);
    save();
    emit tasksChanged();
}

// ---------------------------------------------------------------- сохранение

void JobPanel::save() const
{
    QJsonArray arr;
    for (const TaskInfo &t : tasks_) {
        QJsonObject o;
        o["id"] = QString::number(t.id);
        o["title"] = t.title;
        o["state"] = int(t.state);
        o["error"] = t.error;
        o["openPath"] = t.openPath;
        o["finishedAt"] = QString::number(t.finishedAt);
        QJsonArray jobs;
        for (const JobSpec &j : t.jobs) {
            QJsonObject jo;
            jo["title"] = j.title;
            jo["program"] = j.program;
            jo["args"] = QJsonArray::fromStringList(j.args);
            jo["workDir"] = j.workDir;
            jo["weight"] = j.weight;
            jobs.append(jo);
        }
        o["jobs"] = jobs;
        arr.append(o);
    }
    QSaveFile f(storePath());
    if (!f.open(QIODevice::WriteOnly)) return;
    f.write(QJsonDocument(arr).toJson(QJsonDocument::Compact));
    f.commit();
}

void JobPanel::load()
{
    QFile f(storePath());
    if (!f.open(QIODevice::ReadOnly)) return;
    const QJsonArray arr = QJsonDocument::fromJson(f.readAll()).array();
    for (const QJsonValue &v : arr) {
        const QJsonObject o = v.toObject();
        TaskInfo t;
        t.id = o["id"].toString().toLongLong();
        t.title = o["title"].toString();
        t.state = static_cast<TaskInfo::State>(o["state"].toInt());
        t.error = o["error"].toString();
        t.openPath = o["openPath"].toString();
        t.finishedAt = o["finishedAt"].toString().toLongLong();
        for (const QJsonValue &jv : o["jobs"].toArray()) {
            const QJsonObject jo = jv.toObject();
            JobSpec j;
            j.title = jo["title"].toString();
            j.program = jo["program"].toString();
            for (const QJsonValue &a : jo["args"].toArray()) j.args << a.toString();
            j.workDir = jo["workDir"].toString();
            j.weight = jo["weight"].toInt(1);
            t.jobs << j;
        }
        if (t.id == 0 || t.jobs.isEmpty()) continue;
        if (t.state == TaskInfo::Running) {          // программу закрыли во время работы
            t.state = TaskInfo::Interrupted;
            t.error = TR("job.interrupted");
        }
        tasks_ << t;
    }
}

// ---------------------------------------------------------------- этапы

void JobPanel::startStage(int i)
{
    const JobSpec &j = jobs_[i];
    if (!QFileInfo::exists(j.program)) {
        fail(I18n::instance().t("job.toolMissing", {QDir::toNativeSeparators(j.program)}), false);
        return;
    }
    const int w = qMax(1, j.weight);
    runner_->setProgressRange(done_ * 100 / total_, (done_ + w) * 100 / total_);

    QProcessEnvironment env;
    env.insert(QStringLiteral("PSVIETHOA_SONY_SDK"), AppPaths::sonySdkDir());   // путь к портативному Sony toolchain
    env.insert(QStringLiteral("FPKG_LANG"), QStringLiteral("en"));
    env.insert(QStringLiteral("PYTHONUTF8"), QStringLiteral("1"));          // Python-утилиты (mkpfs, backpork-cli) печатают в UTF-8
    env.insert(QStringLiteral("PYTHONIOENCODING"), QStringLiteral("utf-8"));
    runner_->setExtraEnvironment(env);

    status_->setText(j.title);
    log_->appendPlainText(QStringLiteral("▶ ") + j.title);
    log_->appendPlainText(QStringLiteral("$ ") + QDir::toNativeSeparators(j.program) + QLatin1Char(' ') + j.args.join(QLatin1Char(' ')));
    runner_->start(j.program, j.args, j.workDir);
}

void JobPanel::fail(const QString &why, bool cancelled)
{
    status_->setObjectName(cancelled || closing_ ? "muted" : "warn");
    repolishStatus();
    status_->setText(cancelled ? TR("job.cancelledShort") : closing_ ? TR("job.interrupted") : TR("job.failedShort"));
    log_->appendPlainText(QStringLiteral("✖ ") + why);
    const QString hint = ErrorHints::find(why);
    if (!hint.isEmpty())
        log_->appendPlainText(TR("hint.prefix") + hint);
    finishTask(closing_ ? TaskInfo::Interrupted : cancelled ? TaskInfo::Cancelled : TaskInfo::Failed, why);
}

// Строки вида "[####   ] 45% ..." — это прогресс: в лог не пишем, показываем в статусе.
void JobPanel::onLine(const QString &line, bool)
{
    lastOutput_.restart();
    static const QRegularExpression prog(QStringLiteral(R"(^\[[^\]]*\]\s*\d)"));
    if (prog.match(line).hasMatch()) {
        status_->setText(line);
        return;
    }
    log_->appendPlainText(line);
}

// После смены objectName Qt сам стиль не пересчитывает.
void JobPanel::repolishStatus()
{
    status_->style()->unpolish(status_);
    status_->style()->polish(status_);
}

// Раз в секунду: «Прошло mm:ss»; если утилита давно ничего не печатала — честно говорим об этом, а не «зависаем» молча.
void JobPanel::updateElapsed()
{
    if (!busy_) return;
    QString text = I18n::instance().t("job.elapsed", {clockText(clock_.elapsed() / 1000)});
    const qint64 quiet = lastOutput_.elapsed() / 1000;
    if (quiet >= 15)
        text += I18n::instance().t("job.stalled", {clockText(quiet)});
    if (queuedCount() > 0)
        text += I18n::instance().t("job.queueSuffix", {queuedCount()});
    elapsed_->setText(text);
}

void JobPanel::stopAll()
{
    closing_ = true;                    // текущая задача помечается «прервана» (её можно повторить после перезапуска)
    if (runner_->isRunning())
        runner_->stopAndWait(8000);
}
