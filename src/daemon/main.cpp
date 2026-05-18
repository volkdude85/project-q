// ProjectQd — Persistent C++ farm mesh daemon
// Usage: project-qd --coordinator [--port 9100]
//        project-qd --worker --connect 192.168.0.145:9100 [--name porsche] [--caps gpu,x86_64]

#include "config.h"
#include "coordinator.h"
#include "worker.h"
#include <iostream>
#include <cstdlib>
#include <csignal>

static Coordinator* g_coord = nullptr;
static Worker* g_worker = nullptr;

void handleSignal(int) {
    std::cerr << "\n[QD] Shutting down...\n";
    if (g_coord) g_coord->stop();
    if (g_worker) g_worker->stop();
}

int main(int argc, char** argv) {
    DaemonConfig cfg = DaemonConfig::fromArgs(argc, argv);
    cfg.print();

    // Ensure work and artifact dirs exist
    system(("mkdir -p " + cfg.workDir).c_str());
    system(("mkdir -p " + cfg.artifactDir).c_str());

    // Signal handling
    signal(SIGINT, handleSignal);
    signal(SIGTERM, handleSignal);

    if (cfg.isCoordinator) {
        Coordinator coord(cfg);
        g_coord = &coord;
        if (!coord.start()) {
            std::cerr << "[QD] Coordinator start failed\n";
            return 1;
        }
        std::cout << "[QD] Coordinator running on TCP :" << cfg.tcpPort
                  << " UDP :" << cfg.udpPort << "\n";
        coord.run();
    } else {
        // Try to connect with retries
        for (int attempt = 1; attempt <= 5; attempt++) {
            Worker worker(cfg);
            g_worker = &worker;
            if (worker.start()) {
                worker.run();
                break; // run returns when connection drops
            }
            std::cerr << "[QD] Connection attempt " << attempt << "/5 failed, retrying in 5s...\n";
            sleep(5);
        }
        std::cerr << "[QD] Worker exiting\n";
    }

    return 0;
}
