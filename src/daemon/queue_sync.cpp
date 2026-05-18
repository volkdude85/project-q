#include "queue_sync.h"
#include <sqlite3.h>
#include <cstring>
#include <iostream>

std::vector<QueueTask> QueueSync::getPendingTasks(const std::string& dbPath) {
    std::vector<QueueTask> tasks;
    sqlite3* db = nullptr;

    if (sqlite3_open(dbPath.c_str(), &db) != SQLITE_OK) {
        std::cerr << "[DB] Cannot open: " << sqlite3_errmsg(db) << "\n";
        if (db) sqlite3_close(db);
        return tasks;
    }

    // Enable WAL mode for concurrent access
    sqlite3_exec(db, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr);
    sqlite3_exec(db, "PRAGMA busy_timeout=3000;", nullptr, nullptr, nullptr);

    const char* sql = "SELECT id, name, cmd, deps, status FROM tasks "
                      "WHERE status='PENDING' ORDER BY id";
    sqlite3_stmt* stmt = nullptr;

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            QueueTask t;
            t.id = sqlite3_column_int(stmt, 0);
            const char* nameStr = (const char*)sqlite3_column_text(stmt, 1);
            const char* cmdStr = (const char*)sqlite3_column_text(stmt, 2);
            const char* depsStr = (const char*)sqlite3_column_text(stmt, 3);
            const char* statusStr = (const char*)sqlite3_column_text(stmt, 4);
            if (nameStr) t.name = nameStr;
            if (cmdStr) t.cmd = cmdStr;
            if (depsStr) t.deps = depsStr;
            if (statusStr) t.status = statusStr;
            tasks.push_back(t);
        }
    }

    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return tasks;
}

bool QueueSync::updateTaskStatus(const std::string& dbPath, int taskId,
                                  const std::string& newStatus) {
    sqlite3* db = nullptr;
    if (sqlite3_open(dbPath.c_str(), &db) != SQLITE_OK) {
        std::cerr << "[DB] Cannot open for update: " << sqlite3_errmsg(db) << "\n";
        if (db) sqlite3_close(db);
        return false;
    }

    sqlite3_exec(db, "PRAGMA busy_timeout=5000;", nullptr, nullptr, nullptr);

    char sql[256];
    if (newStatus == "RUNNING") {
        snprintf(sql, sizeof(sql),
                 "UPDATE tasks SET status='RUNNING', assigned_node='%s', "
                 "started_at=datetime('now') WHERE id=%d",
                 "qd-daemon", taskId);
    } else if (newStatus == "DONE") {
        snprintf(sql, sizeof(sql),
                 "UPDATE tasks SET status='DONE', completed_at=datetime('now') "
                 "WHERE id=%d", taskId);
    } else if (newStatus == "FAILED") {
        snprintf(sql, sizeof(sql),
                 "UPDATE tasks SET status='FAILED', completed_at=datetime('now') "
                 "WHERE id=%d", taskId);
    } else {
        snprintf(sql, sizeof(sql),
                 "UPDATE tasks SET status='%s' WHERE id=%d",
                 newStatus.c_str(), taskId);
    }

    char* errMsg = nullptr;
    int rc = sqlite3_exec(db, sql, nullptr, nullptr, &errMsg);
    if (rc != SQLITE_OK) {
        std::cerr << "[DB] Update failed: " << (errMsg ? errMsg : "unknown") << "\n";
        sqlite3_free(errMsg);
        sqlite3_close(db);
        return false;
    }

    sqlite3_close(db);
    return true;
}

bool QueueSync::resetTask(const std::string& dbPath, int taskId) {
    sqlite3* db = nullptr;
    if (sqlite3_open(dbPath.c_str(), &db) != SQLITE_OK) {
        if (db) sqlite3_close(db);
        return false;
    }

    char sql[256];
    snprintf(sql, sizeof(sql),
             "UPDATE tasks SET status='PENDING', assigned_node='', "
             "started_at='', completed_at='', error_log='' WHERE id=%d",
             taskId);

    char* errMsg = nullptr;
    int rc = sqlite3_exec(db, sql, nullptr, nullptr, &errMsg);
    if (rc != SQLITE_OK) {
        sqlite3_free(errMsg);
        sqlite3_close(db);
        return false;
    }

    sqlite3_close(db);
    return true;
}
