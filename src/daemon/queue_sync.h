#pragma once
#include <string>
#include <vector>

struct QueueTask {
    int id = 0;
    std::string name;
    std::string cmd;
    std::string deps;
    std::string status;
};

namespace QueueSync {
    // Get all PENDING tasks from SQLite
    std::vector<QueueTask> getPendingTasks(const std::string& dbPath);

    // Get a task's outputs field by ID
    std::string getTaskOutputs(const std::string& dbPath, int taskId);

    // Update a task's status
    bool updateTaskStatus(const std::string& dbPath, int taskId,
                          const std::string& newStatus);

    // Reset a task to PENDING (for re-queue after timeout)
    bool resetTask(const std::string& dbPath, int taskId);
}
