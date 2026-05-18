#include "config.h"
#include <iostream>
#include <cstring>
#include <unistd.h>
#include <cstdlib>

DaemonConfig DaemonConfig::fromArgs(int argc, char** argv) {
    DaemonConfig cfg;
    char hostname[256];
    if (gethostname(hostname, sizeof(hostname)) == 0) {
        cfg.nodeName = hostname;
    } else {
        cfg.nodeName = "unknown";
    }

    // Default paths
    const char* home = getenv("HOME");
    if (home) {
        cfg.dbPath = std::string(home) + "/.local/share/project-q/queue.db";
        cfg.artifactDir = std::string(home) + "/.local/share/project-q/cache/artifacts";
    } else {
        cfg.dbPath = "/tmp/project-q-queue.db";
        cfg.artifactDir = "/tmp/project-q-artifacts";
    }

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--coordinator") == 0) {
            cfg.isCoordinator = true;
        } else if (strcmp(argv[i], "--worker") == 0) {
            cfg.isCoordinator = false;
        } else if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            cfg.tcpPort = static_cast<uint16_t>(std::stoi(argv[++i]));
            cfg.udpPort = cfg.tcpPort + 1;
        } else if (strcmp(argv[i], "--connect") == 0 && i + 1 < argc) {
            // Parse "ip:port" or just "ip"
            std::string conn = argv[++i];
            auto colon = conn.find(':');
            if (colon != std::string::npos) {
                cfg.coordinatorHost = conn.substr(0, colon);
                cfg.tcpPort = static_cast<uint16_t>(std::stoi(conn.substr(colon + 1)));
                cfg.udpPort = cfg.tcpPort + 1;
            } else {
                cfg.coordinatorHost = conn;
            }
        } else if (strcmp(argv[i], "--name") == 0 && i + 1 < argc) {
            cfg.nodeName = argv[++i];
        } else if (strcmp(argv[i], "--caps") == 0 && i + 1 < argc) {
            cfg.capabilities = argv[++i];
        }
    }

    return cfg;
}

void DaemonConfig::print() const {
    std::cerr << "Project-Qd config:\n"
              << "  Mode: " << (isCoordinator ? "COORDINATOR" : "WORKER") << "\n"
              << "  Node: " << nodeName << "\n"
              << "  TCP port: " << tcpPort << "\n"
              << "  UDP port: " << udpPort << "\n"
              << "  Coordinator: " << coordinatorHost << "\n"
              << "  DB: " << dbPath << "\n"
              << "  Artifacts: " << artifactDir << "\n"
              << "  Caps: " << (capabilities.empty() ? "(none)" : capabilities) << "\n";
}
