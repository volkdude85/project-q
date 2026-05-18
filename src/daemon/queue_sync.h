#ifndef PROJECTQD_QUEUE_SYNC_H
#define PROJECTQD_QUEUE_SYNC_H

#include <QObject>
#include <QSqlDatabase>
#include <QJsonArray>
#include "config.h"
#include "coordinator.h"

class QueueSync : public QObject {
    Q_OBJECT
public:
    explicit QueueSync(const DaemonConfig &config, Coordinator *coordinator, QObject *parent = nullptr);
    ~QueueSync();

    bool open();
    void close();

    int addTask(const QString &name, const QString &command, int timeoutSec = 1800);
    bool markTask(int taskId, const QString &status);
    QJsonArray listTasks(const QString &statusFilter = QString());

private:
    bool createSchema();

    DaemonConfig m_config;
    Coordinator *m_coordinator = nullptr;
    QSqlDatabase m_db;
};

#endif
