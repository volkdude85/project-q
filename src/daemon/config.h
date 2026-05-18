#pragma once
#include <string>
#include <cstdint>

struct DaemonConfig {
    // Coordinator mode
    bool isCoordinator = false;

    // Network
    uint16_t tcpPort = 9100;
    uint16_t udpPort = 9101;
    std::string coordinatorHost = "192.168.0.145";

    // Node identity
    std::string nodeName;
    std::string capabilities; // e.g. "gpu,x86_64,32gb_ram"

    // Heartbeat
    int heartbeatIntervalSec = 5;
    int heartbeatTimeoutSec = 20;

    // Task execution
    int taskTimeoutSec = 1800;
    std::string workDir = "/tmp/project-qd-work";
    std::string dbPath;
    std::string artifactDir;

    static DaemonConfig fromArgs(int argc, char** argv);
    void print() const;
};
