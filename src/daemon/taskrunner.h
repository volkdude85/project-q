#ifndef PROJECTQD_TASKRUNNER_H
#define PROJECTQD_TASKRUNNER_H

#include <QObject>
#include <QProcess>
#include <QTimer>
#include <QElapsedTimer>
#include <QStringList>

class TaskRunner : public QObject {
    Q_OBJECT
public:
    explicit TaskRunner(int taskId,
                        const QString &name,
                        const QString &command,
                        const QString &workDir,
                        int timeoutSec = 1800,
                        QObject *parent = nullptr);
    ~TaskRunner();

    void start();
    void kill();

    int taskId() const { return m_taskId; }
    QString status() const { return m_status; }

signals:
    void completed(const QString &status, int durationSec,
                   const QStringList &outputs, const QString &errorLog);

private slots:
    void onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void onTimeout();

private:
    int m_taskId;
    QString m_name;
    QString m_command;
    QString m_workDir;
    int m_timeoutSec;

    QProcess *m_process = nullptr;
    QTimer *m_timeoutTimer = nullptr;
    QElapsedTimer m_elapsed;
    QString m_status;
    QString m_outputDir;
};

#endif
