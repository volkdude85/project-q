#include "taskrunner.h"
#include <cstring>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <signal.h>
#include <chrono>
#include <thread>
#include <fcntl.h>
#include <cstdlib>

TaskResult TaskRunner::execute(const std::string& cmd, int timeoutSec,
                                const std::string& workDir) {
    TaskResult result;

    // Create pipes for stdout and stderr
    int outPipe[2], errPipe[2];
    if (pipe(outPipe) < 0 || pipe(errPipe) < 0) {
        result.stderr = "pipe() failed";
        return result;
    }

    // Make write ends non-blocking to avoid deadlocks
    fcntl(outPipe[1], F_SETFL, O_NONBLOCK);
    fcntl(errPipe[1], F_SETFL, O_NONBLOCK);

    pid_t pid = fork();
    if (pid < 0) {
        result.stderr = "fork() failed";
        close(outPipe[0]); close(outPipe[1]);
        close(errPipe[0]); close(errPipe[1]);
        return result;
    }

    if (pid == 0) {
        // Child process
        close(outPipe[0]);
        close(errPipe[0]);
        dup2(outPipe[1], STDOUT_FILENO);
        dup2(errPipe[1], STDERR_FILENO);
        close(outPipe[1]);
        close(errPipe[1]);

        // Change to work directory
        if (!workDir.empty()) chdir(workDir.c_str());

        // Execute via shell
        execl("/bin/sh", "sh", "-c", cmd.c_str(), nullptr);
        _exit(127); // exec failed
    }

    // Parent process — close write ends
    close(outPipe[1]);
    close(errPipe[1]);

    // Wait for process with timeout
    auto start = std::chrono::steady_clock::now();
    int status = 0;
    bool timedOut = false;

    while (true) {
        pid_t wpid = waitpid(pid, &status, WNOHANG);
        if (wpid == pid) {
            result.durationSec = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now() - start).count();
            if (WIFEXITED(status)) result.exitCode = WEXITSTATUS(status);
            else result.exitCode = -1;
            break;
        }

        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - start).count();
        if (elapsed >= timeoutSec) {
            // Kill the child
            kill(pid, SIGTERM);
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            kill(pid, SIGKILL);
            waitpid(pid, &status, 0);
            result.exitCode = -1;
            result.durationSec = timeoutSec;
            timedOut = true;
            break;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    // Read output
    char buf[4096];
    int n;
    while ((n = read(outPipe[0], buf, sizeof(buf) - 1)) > 0) {
        buf[n] = 0;
        result.stdout += buf;
    }
    while ((n = read(errPipe[0], buf, sizeof(buf) - 1)) > 0) {
        buf[n] = 0;
        result.stderr += buf;
    }

    close(outPipe[0]);
    close(errPipe[0]);

    if (timedOut) {
        result.stderr += "\n[TIMEOUT] Task exceeded " + std::to_string(timeoutSec) + "s";
    }

    return result;
}
