#include "coordinator.h"
#include "protocol.h"
#include "config.h"
#include "queue_sync.h"

#include <iostream>
#include <cstring>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <ctime>
#include <algorithm>

Coordinator::Coordinator(const DaemonConfig& cfg) : config(cfg) {}
Coordinator::~Coordinator() { stop(); }

bool Coordinator::start() {
    // ── TCP listen socket ────────────────────────────────────────
    tcpFd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (tcpFd < 0) { perror("TCP socket"); return false; }

    int opt = 1;
    setsockopt(tcpFd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(config.tcpPort);
    addr.sin_addr.s_addr = INADDR_ANY;
    if (bind(tcpFd, (sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("TCP bind"); close(tcpFd); return false;
    }
    listen(tcpFd, 16);

    // ── UDP heartbeat socket ─────────────────────────────────────
    udpFd = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, 0);
    if (udpFd < 0) { perror("UDP socket"); close(tcpFd); return false; }

    sockaddr_in udpAddr{};
    udpAddr.sin_family = AF_INET;
    udpAddr.sin_port = htons(config.udpPort);
    udpAddr.sin_addr.s_addr = INADDR_ANY;
    if (bind(udpFd, (sockaddr*)&udpAddr, sizeof(udpAddr)) < 0) {
        perror("UDP bind"); close(tcpFd); close(udpFd); return false;
    }

    // ── Epoll ────────────────────────────────────────────────────
    epollFd = epoll_create1(0);
    if (epollFd < 0) { perror("epoll"); return false; }

    auto addFd = [this](int fd, uint32_t events) {
        epoll_event ev{};
        ev.events = events;
        ev.data.fd = fd;
        epoll_ctl(epollFd, EPOLL_CTL_ADD, fd, &ev);
    };

    addFd(tcpFd, EPOLLIN);
    addFd(udpFd, EPOLLIN);

    std::cerr << "[COORD] Listening on TCP :" << config.tcpPort
              << " UDP :" << config.udpPort << "\n";
    running = true;
    return true;
}

void Coordinator::run() {
    const int MAX_EVENTS = 64;
    epoll_event events[MAX_EVENTS];

    while (running) {
        int nfds = epoll_wait(epollFd, events, MAX_EVENTS, 1000); // 1s timeout
        if (nfds < 0) break;

        for (int i = 0; i < nfds; i++) {
            int fd = events[i].data.fd;

            if (fd == tcpFd) {
                acceptTcp();
            } else if (fd == udpFd) {
                handleUdpRead();
            } else {
                handleTcpRead(fd);
            }
        }

        // Check heartbeats and scan for pending tasks every iteration
        checkHeartbeats();
        scanPendingTasks();
    }
}

void Coordinator::stop() {
    running = false;
    if (tcpFd >= 0) close(tcpFd);
    if (udpFd >= 0) close(udpFd);
    if (epollFd >= 0) close(epollFd);
    // Close TCP clients
    for (auto& [fd, _] : tcpClients) close(fd);
    tcpClients.clear();
}

// ── Accept new TCP connection ────────────────────────────────────
void Coordinator::acceptTcp() {
    sockaddr_in clientAddr{};
    socklen_t addrLen = sizeof(clientAddr);
    int clientFd = accept4(tcpFd, (sockaddr*)&clientAddr, &addrLen, SOCK_NONBLOCK);
    if (clientFd < 0) return;

    char ipStr[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &clientAddr.sin_addr, ipStr, sizeof(ipStr));
    std::cerr << "[COORD] TCP connection from " << ipStr << "\n";

    epoll_event ev{};
    ev.events = EPOLLIN | EPOLLRDHUP;
    ev.data.fd = clientFd;
    epoll_ctl(epollFd, EPOLL_CTL_ADD, clientFd, &ev);
    tcpClients[clientFd] = ""; // not yet registered
}

// ── Handle TCP data ──────────────────────────────────────────────
void Coordinator::handleTcpRead(int fd) {
    char buf[4096];
    int n = read(fd, buf, sizeof(buf));
    if (n <= 0) {
        closeClient(fd);
        return;
    }

    // Append to read buffer
    readBufs[fd].append(buf, n);
    auto& readBuf = readBufs[fd];

    // Try to parse frames
    while (true) {
        TcpFrame frame;
        int consumed = TcpFrame::decode(readBuf.data(), readBuf.size(), frame);
        if (consumed == 0) break; // need more data

        readBuf.erase(0, consumed);

        // Process the message
        std::string cmd = Proto::parseCmd(frame.data);

        if (cmd == "register") {
            // Extract node name — naive string search
            auto nPos = frame.data.find("\"node\"");
            if (nPos != std::string::npos) {
                auto colon = frame.data.find(':', nPos + 6);
                auto q1 = frame.data.find('"', colon + 1);
                auto q2 = frame.data.find('"', q1 + 1);
                if (q1 != std::string::npos && q2 != std::string::npos) {
                    std::string nodeName = frame.data.substr(q1 + 1, q2 - q1 - 1);

                    // Find caps
                    auto cPos = frame.data.find("\"capabilities\"");
                    std::string caps;
                    if (cPos != std::string::npos) {
                        auto col2 = frame.data.find(':', cPos + 14);
                        auto q3 = frame.data.find('"', col2 + 1);
                        auto q4 = frame.data.find('"', q3 + 1);
                        if (q3 != std::string::npos && q4 != std::string::npos)
                            caps = frame.data.substr(q3 + 1, q4 - q3 - 1);
                    }

                    nodes[nodeName].name = nodeName;
                    nodes[nodeName].capabilities = caps;
                    nodes[nodeName].connected = true;
                    nodes[nodeName].lastHeard = time(nullptr);
                    tcpClients[fd] = nodeName;

                    std::cerr << "[COORD] Node '" << nodeName << "' registered"
                              << (caps.empty() ? "" : " (caps: " + caps + ")")
                              << "\n";

                    // Check for pending tasks
                    auto pending = QueueSync::getPendingTasks(config.dbPath);
                    for (const auto& task : pending) {
                        if (nodes[nodeName].currentTask < 0) {
                            dispatchTask(nodeName, task.id, task.name, task.cmd);
                        }
                    }
                }
            }
        } else if (cmd == "task_result") {
            // Parse task_id, status, duration_sec
            auto tPos = frame.data.find("\"task_id\"");
            int taskId = 0;
            if (tPos != std::string::npos) {
                auto col = frame.data.find(':', tPos + 9);
                auto end = frame.data.find_first_of(",}", col + 1);
                taskId = std::stoi(frame.data.substr(col + 1, end - col - 1));
            }

            auto sPos = frame.data.find("\"status\"");
            std::string status = "failed";
            if (sPos != std::string::npos) {
                auto col = frame.data.find(':', sPos + 8);
                auto q1 = frame.data.find('"', col + 1);
                auto q2 = frame.data.find('"', q1 + 1);
                if (q1 != std::string::npos && q2 != std::string::npos)
                    status = frame.data.substr(q1 + 1, q2 - q1 - 1);
            }

            auto dPos = frame.data.find("\"duration_sec\"");
            int dur = 0;
            if (dPos != std::string::npos) {
                auto col = frame.data.find(':', dPos + 14);
                auto end = frame.data.find_first_of(",}", col + 1);
                dur = std::stoi(frame.data.substr(col + 1, end - col - 1));
            }

            std::string nodeName = tcpClients[fd];
            if (!nodeName.empty()) {
                nodes[nodeName].currentTask = -1;
                QueueSync::updateTaskStatus(config.dbPath, taskId,
                                            status == "done" ? "DONE" : "FAILED");
                std::cerr << "[COORD] Task #" << taskId << " from '" << nodeName
                          << "': " << status << " (" << dur << "s)\n";
            }

        } else if (cmd == "heartbeat_ack") {
            // Worker responded to heartbeat
            auto lPos = frame.data.find("\"load\"");
            uint32_t load = 0;
            if (lPos != std::string::npos) {
                auto col = frame.data.find(':', lPos + 6);
                auto end = frame.data.find_first_of(",}", col + 1);
                load = std::stoul(frame.data.substr(col + 1, end - col - 1));
            }
            std::string nodeName = tcpClients[fd];
            if (!nodeName.empty()) {
                nodes[nodeName].load = load;
                nodes[nodeName].lastHeard = time(nullptr);
            }
        }
    }
}

// ── Handle UDP heartbeat ─────────────────────────────────────────
void Coordinator::handleUdpRead() {
    HeartbeatPacket hb;
    sockaddr_in from{};
    socklen_t fromLen = sizeof(from);
    int n = recvfrom(udpFd, &hb, sizeof(hb), 0, (sockaddr*)&from, &fromLen);
    if (n != (int)sizeof(hb)) return;
    if (hb.magic != 0x50514D00) return;

    std::string nodeName(hb.nodeId);
    // Truncate at first null
    auto nullPos = nodeName.find('\0');
    if (nullPos != std::string::npos) nodeName = nodeName.substr(0, nullPos);

    if (nodeName.empty()) return;

    nodes[nodeName].name = nodeName;
    nodes[nodeName].addr = from;
    nodes[nodeName].load = hb.load;
    nodes[nodeName].lastHeard = time(nullptr);
    nodes[nodeName].connected = true;
}

// ── Check for dead nodes ─────────────────────────────────────────
void Coordinator::checkHeartbeats() {
    time_t now = time(nullptr);
    for (auto& [name, node] : nodes) {
        if (node.connected && (now - node.lastHeard) > config.heartbeatTimeoutSec) {
            std::cerr << "[COORD] Node '" << name << "' timed out ("
                      << (now - node.lastHeard) << "s since last heartbeat)\n";
            node.connected = false;
            // Re-queue any running task
            if (node.currentTask >= 0) {
                QueueSync::resetTask(config.dbPath, node.currentTask);
                node.currentTask = -1;
            }
        }
    }
}

// ── Scan for pending tasks and dispatch to idle nodes ───────────
void Coordinator::scanPendingTasks() {
    auto pending = QueueSync::getPendingTasks(config.dbPath);
    if (pending.empty()) return;

    // Find idle connected nodes
    for (const auto& task : pending) {
        // Check if already dispatched
        bool alreadyAssigned = false;
        for (const auto& [name, node] : nodes) {
            if (node.currentTask == task.id) { alreadyAssigned = true; break; }
        }
        if (alreadyAssigned) continue;

        // Pick first idle, connected node
        for (auto& [name, node] : nodes) {
            if (node.connected && node.currentTask < 0) {
                dispatchTask(name, task.id, task.name, task.cmd);
                break; // dispatch one task per scan cycle
            }
        }
    }
}

// ── Dispatch a task to a specific node ───────────────────────────
void Coordinator::dispatchTask(const std::string& nodeName,
                                int taskId, const std::string& taskName,
                                const std::string& cmd) {
    // Find the node's TCP fd
    for (auto& [fd, name] : tcpClients) {
        if (name == nodeName) {
            std::string msg = Proto::makeTaskAssign(taskId, taskName, cmd);
            sendTcp(fd, msg);
            nodes[nodeName].currentTask = taskId;
            QueueSync::updateTaskStatus(config.dbPath, taskId, "RUNNING");
            std::cerr << "[COORD] Task #" << taskId << " (" << taskName
                      << ") assigned to '" << nodeName << "'\n";
            return;
        }
    }
    std::cerr << "[COORD] Cannot dispatch task #" << taskId
              << " — node '" << nodeName << "' has no TCP connection\n";
}

void Coordinator::sendTcp(int fd, const std::string& msg) {
    auto frame = TcpFrame::encode(msg);
    write(fd, frame.data(), frame.size());
}

void Coordinator::closeClient(int fd) {
    std::string name = tcpClients[fd];
    if (!name.empty()) {
        nodes[name].connected = false;
        std::cerr << "[COORD] Node '" << name << "' disconnected\n";
    }
    tcpClients.erase(fd);
    readBufs.erase(fd);
    epoll_ctl(epollFd, EPOLL_CTL_DEL, fd, nullptr);
    close(fd);
}
