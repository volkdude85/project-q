#include "queue_sync.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QDebug>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>

QueueSync::QueueSync(const DaemonConfig &config, Coordinator *coordinator, QObject *parent)
    : QObject(parent), m_config(config), m_coordinator(coordinator)
{
    m_db = QSqlDatabase::addDatabase("QSQLITE", "project-qd");
    m_db.setDatabaseName(config.queueDbPath);
}

QueueSync::~QueueSync() { close(); }

bool QueueSync::open() {
    if (!m_db.open()) {
        qCritical() << "[QSYNC] Failed to open database:" << m_db.lastError().text();
        return false;
    }

    if (!createSchema()) {
        qCritical() << "[QSYNC] Failed to create schema";
        return false;
    }

    qDebug() << "[QSYNC] Opened queue database at" << m_config.queueDbPath;

    // Load existing pending tasks into coordinator
    QJsonArray pending = listTasks("pending");
    for (const auto &task : pending) {
        QJsonObject t = task.toObject();
        m_coordinator->taskQueued(t["id"].toInt());
    }

    return true;
}

void QueueSync::close() {
    if (m_db.isOpen()) m_db.close();
}

bool QueueSync::createSchema() {
    QSqlQuery query(m_db);

    QString sql = R"(
        CREATE TABLE IF NOT EXISTS tasks (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            name TEXT NOT NULL,
            command TEXT NOT NULL,
            status TEXT NOT NULL DEFAULT 'pending',
            assigned_node TEXT,
            timeout_sec INTEGER DEFAULT 1800,
            duration_sec INTEGER DEFAULT 0,
            stdout_path TEXT,
            stderr_path TEXT,
            created_at TEXT NOT NULL DEFAULT (datetime('now')),
            started_at TEXT,
            finished_at TEXT
        )
    )";

    if (!query.exec(sql)) {
        qCritical() << "[QSYNC] Schema creation failed:" << query.lastError().text();
        return false;
    }

    return true;
}

int QueueSync::addTask(const QString &name, const QString &command, int timeoutSec) {
    QSqlQuery query(m_db);
    query.prepare("INSERT INTO tasks (name, command, timeout_sec) VALUES (?, ?, ?)");
    query.addBindValue(name);
    query.addBindValue(command);
    query.addBindValue(timeoutSec);

    if (!query.exec()) {
        qCritical() << "[QSYNC] Failed to add task:" << query.lastError().text();
        return -1;
    }

    int taskId = query.lastInsertId().toInt();
    qDebug() << "[QSYNC] Added task #" << taskId << ":" << name;

    m_coordinator->taskQueued(taskId);

    return taskId;
}

bool QueueSync::markTask(int taskId, const QString &status) {
    QSqlQuery query(m_db);

    if (status == "running") {
        query.prepare("UPDATE tasks SET status = ?, started_at = datetime('now') WHERE id = ?");
    } else if (status == "done" || status == "failed" || status == "timeout") {
        query.prepare("UPDATE tasks SET status = ?, finished_at = datetime('now') WHERE id = ?");
    } else {
        query.prepare("UPDATE tasks SET status = ? WHERE id = ?");
    }

    query.addBindValue(status);
    query.addBindValue(taskId);

    if (!query.exec()) {
        qCritical() << "[QSYNC] Failed to update task #" << taskId << ":" << query.lastError().text();
        return false;
    }

    return true;
}

QJsonArray QueueSync::listTasks(const QString &statusFilter) {
    QJsonArray tasks;
    QSqlQuery query(m_db);

    QString sql = "SELECT id, name, command, status, assigned_node, timeout_sec, "
                  "duration_sec, created_at, started_at, finished_at FROM tasks";
    if (!statusFilter.isEmpty()) {
        sql += " WHERE status = ?";
    }
    sql += " ORDER BY id DESC LIMIT 100";

    query.prepare(sql);
    if (!statusFilter.isEmpty()) {
        query.addBindValue(statusFilter);
    }

    if (!query.exec()) {
        qCritical() << "[QSYNC] Failed to list tasks:" << query.lastError().text();
        return tasks;
    }

    while (query.next()) {
        QJsonObject task;
        task["id"] = query.value(0).toInt();
        task["name"] = query.value(1).toString();
        task["command"] = query.value(2).toString();
        task["status"] = query.value(3).toString();
        task["assigned_node"] = query.value(4).toString();
        task["timeout_sec"] = query.value(5).toInt();
        task["duration_sec"] = query.value(6).toInt();
        task["created_at"] = query.value(7).toString();
        task["started_at"] = query.value(8).toString();
        task["finished_at"] = query.value(9).toString();
        tasks.append(task);
    }

    return tasks;
}

int QueueSync::purgeOldTasks(int retentionDays) {
    QSqlQuery query(m_db);
    query.prepare(
        "SELECT id, name, command, stdout_path, stderr_path, status, "
        "duration_sec, created_at, finished_at FROM tasks "
        "WHERE status IN ('done','failed','timeout') "
        "AND finished_at < datetime('now', '-' || ? || ' days')"
    );
    query.addBindValue(retentionDays);

    if (!query.exec()) {
        qCritical() << "[QSYNC] Failed to query old tasks:" << query.lastError().text();
        return -1;
    }

    QString archiveDir = m_config.dataDir + "/archive/"
        + QDateTime::currentDateTimeUtc().toString("yyyy-MM");

    int purged = 0;
    QSqlQuery del(m_db);
    del.prepare("DELETE FROM tasks WHERE id = ?");

    while (query.next()) {
        int taskId = query.value(0).toInt();
        QJsonObject task;
        task["id"] = taskId;
        task["name"] = query.value(1).toString();
        task["command"] = query.value(2).toString();
        task["stdout_path"] = query.value(3).toString();
        task["stderr_path"] = query.value(4).toString();
        task["status"] = query.value(5).toString();
        task["duration_sec"] = query.value(6).toInt();
        task["created_at"] = query.value(7).toString();
        task["finished_at"] = query.value(8).toString();
        task["archived_at"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);

        QDir().mkpath(archiveDir);
        QFile file(archiveDir + QString("/task-%1.json").arg(taskId));
        if (file.open(QIODevice::WriteOnly)) {
            file.write(QJsonDocument(task).toJson(QJsonDocument::Indented));
            file.close();
        }

        del.addBindValue(taskId);
        if (del.exec()) purged++;
    }

    qDebug() << "[QSYNC] Purged" << purged << "old tasks (>" << retentionDays << "days) to" << archiveDir;
    return purged;
}
