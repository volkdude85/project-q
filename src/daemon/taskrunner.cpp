#include "taskrunner.h"
#include <QDir>
#include <QFile>
#include <QDebug>

TaskRunner::TaskRunner(int taskId, const QString &name, const QString &command,
                       const QString &workDir, int timeoutSec, QObject *parent)
    : QObject(parent)
    , m_taskId(taskId)
    , m_name(name)
    , m_command(command)
    , m_workDir(workDir)
    , m_timeoutSec(timeoutSec)
    , m_status("pending")
{
    m_outputDir = workDir + "/task-" + QString::number(taskId);
}

TaskRunner::~TaskRunner() {
    if (m_process) {
        if (m_process->state() != QProcess::NotRunning) {
            m_process->kill();
            m_process->waitForFinished(3000);
        }
    }
}

void TaskRunner::start() {
    // Create output directory
    QDir().mkpath(m_outputDir);

    m_status = "running";

    m_process = new QProcess(this);
    m_process->setWorkingDirectory(m_workDir);
    m_process->setProcessChannelMode(QProcess::SeparateChannels);

    // Redirect stdout and stderr to files
    QString stdoutPath = m_outputDir + "/stdout.log";
    QString stderrPath = m_outputDir + "/stderr.log";
    m_process->setStandardOutputFile(stdoutPath);
    m_process->setStandardErrorFile(stderrPath);

    connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &TaskRunner::onProcessFinished);

    qDebug() << "[TASK" << m_taskId << "] Starting:" << m_command.left(80);

    m_elapsed.start();

    // Timeout timer
    m_timeoutTimer = new QTimer(this);
    m_timeoutTimer->setSingleShot(true);
    connect(m_timeoutTimer, &QTimer::timeout, this, &TaskRunner::onTimeout);
    m_timeoutTimer->start(m_timeoutSec * 1000);

    // Execute via shell
#ifdef Q_OS_WIN
    m_process->start("cmd.exe", QStringList() << "/c" << m_command);
#else
    m_process->start("/bin/sh", QStringList() << "-c" << m_command);
#endif
}

void TaskRunner::kill() {
    if (m_process && m_process->state() != QProcess::NotRunning) {
        qDebug() << "[TASK" << m_taskId << "] Killing...";
        m_process->kill();
    }
    m_status = "killed";
}

void TaskRunner::onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus) {
    m_timeoutTimer->stop();
    int durationMs = static_cast<int>(m_elapsed.elapsed());
    int durationSec = durationMs / 1000;

    if (exitStatus == QProcess::CrashExit) {
        m_status = "failed";
    } else if (exitCode == 0) {
        m_status = "done";
    } else {
        m_status = "failed";
    }

    // Read output files
    QStringList outputs;
    QString stdoutPath = m_outputDir + "/stdout.log";
    QString stderrPath = m_outputDir + "/stderr.log";

    QFile stdoutFile(stdoutPath);
    if (stdoutFile.open(QIODevice::ReadOnly)) {
        outputs.append(stdoutPath);
        stdoutFile.close();
    }

    QString errorLog;
    QFile stderrFile(stderrPath);
    if (stderrFile.open(QIODevice::ReadOnly)) {
        errorLog = QString::fromUtf8(stderrFile.readAll().left(4096)); // cap at 4KB
        stderrFile.close();
    }

    qDebug() << "[TASK" << m_taskId << "] Finished:" << m_status << "(" << durationSec << "s, exit:" << exitCode << ")";

    emit completed(m_status, durationSec, outputs, errorLog);
}

void TaskRunner::onTimeout() {
    qDebug() << "[TASK" << m_taskId << "] Timeout after" << m_timeoutSec << "s";
    m_status = "timeout";
    kill();
}
