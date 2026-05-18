#pragma once
#include <string>

struct TaskResult {
    int exitCode = -1;
    int durationSec = 0;
    std::string stdout;
    std::string stderr;
};

class TaskRunner {
public:
    TaskResult execute(const std::string& cmd, int timeoutSec,
                       const std::string& workDir);
};
