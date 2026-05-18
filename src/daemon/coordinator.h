#pragma once
#include <string>
#include <unordered_map>
#include <sys/epoll.h>
#include <arpa/inet.h>

class Coordinator {
public:
    explicit Coordinator(const class DaemonConfig& cfg);
    ~Coordinator();

    bool start();
    void run();
    void stop();

private:
    struct NodeInfo {
        std::string name;
        std::string capabilities;
        sockaddr_in addr;       // UDP address for heartbeats
        time_t lastHeard = 0;
        int currentTask = -1;   // -1 = idle
        uint32_t load = 0;
        bool connected = false;
    };

    const DaemonConfig& config;
    int tcpFd = -1;             // TCP listen socket
    int udpFd = -1;             // UDP heartbeat socket
    int epollFd = -1;
    bool running = false;

    // Node registry: keyed by node name
    std::unordered_map<std::string, NodeInfo> nodes;

    // TCP client connections: fd -> node name
    std::unordered_map<int, std::string> tcpClients;

    // Buffer for partial TCP reads
    std::unordered_map<int, std::string> readBufs;

    void acceptTcp();
    void handleTcpRead(int fd);
    void handleUdpRead();
    void checkHeartbeats();
    void dispatchTask(const std::string& nodeName, int taskId,
                      const std::string& taskName, const std::string& cmd);
    void scanPendingTasks();
    void sendTcp(int fd, const std::string& msg);
    void closeClient(int fd);
};
