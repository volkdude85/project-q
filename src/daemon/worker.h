#pragma once
#include <string>
#include <thread>
#include <atomic>

class Worker {
public:
    explicit Worker(const class DaemonConfig& cfg);
    ~Worker();

    bool start();
    void run();
    void stop();

private:
    const DaemonConfig& config;
    int tcpFd = -1;
    int udpFd = -1;
    std::atomic<bool> running{false};
    std::thread heartbeatThread;

    // Read buffer for TCP
    std::string readBuf;

    bool connectTcp();
    bool sendRegister();
    void sendHeartbeat();
    void handleMessage(const std::string& msg);
    void runTask(int taskId, const std::string& name, const std::string& cmd);
};
