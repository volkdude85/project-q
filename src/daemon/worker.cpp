#include "worker.h"
#include "protocol.h"
#include "config.h"
#include "taskrunner.h"
#include "queue_sync.h"
#include "artifacts.h"

#include <iostream>
#include <cstring>
#include <cstring>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <chrono>
#include <thread>

Worker::Worker(const DaemonConfig& cfg) : config(cfg) {}
Worker::~Worker() { stop(); }

bool Worker::start() {
    // Create UDP socket for heartbeats
    udpFd = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, 0);
    if (udpFd < 0) { perror("Worker UDP socket"); return false; }

    // Connect TCP
    if (!connectTcp()) return false;
    if (!sendRegister()) return false;

    // Start heartbeat thread
    heartbeatThread = std::thread(&Worker::sendHeartbeat, this);

    running = true;
    std::cerr << "[WORKER] Connected to coordinator at "
              << config.coordinatorHost << ":" << config.tcpPort << "\n";
    return true;
}

bool Worker::connectTcp() {
    tcpFd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (tcpFd < 0) { perror("Worker TCP socket"); return false; }

    // Resolve coordinator host
    hostent* server = gethostbyname(config.coordinatorHost.c_str());
    if (!server) {
        std::cerr << "[WORKER] Cannot resolve '" << config.coordinatorHost << "'\n";
        return false;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(config.tcpPort);
    memcpy(&addr.sin_addr.s_addr, server->h_addr, server->h_length);

    // Non-blocking connect (we'll poll for completion)
    int rc = connect(tcpFd, (sockaddr*)&addr, sizeof(addr));
    if (rc < 0 && errno != EINPROGRESS) {
        perror("Worker TCP connect");
        close(tcpFd);
        return false;
    }

    // Wait for connection with poll
    fd_set wset;
    FD_ZERO(&wset);
    FD_SET(tcpFd, &wset);
    timeval tv{5, 0}; // 5 second timeout
    rc = select(tcpFd + 1, nullptr, &wset, nullptr, &tv);
    if (rc <= 0) {
        std::cerr << "[WORKER] TCP connect timed out\n";
        close(tcpFd);
        return false;
    }

    // Make it blocking for read simplicity
    int flags = fcntl(tcpFd, F_GETFL, 0);
    // Keep non-blocking for now
    return true;
}

bool Worker::sendRegister() {
    std::string msg = Proto::makeRegister(config.nodeName, config.capabilities);
    auto frame = TcpFrame::encode(msg);
    int n = write(tcpFd, frame.data(), frame.size());
    return n > 0;
}

void Worker::sendHeartbeat() {
    HeartbeatPacket hb;
    hb.magic = 0x50514D00;
    memset(hb.nodeId, 0, sizeof(hb.nodeId));
    strncpy(hb.nodeId, config.nodeName.c_str(), sizeof(hb.nodeId) - 1);

    sockaddr_in coordAddr{};
    coordAddr.sin_family = AF_INET;
    coordAddr.sin_port = htons(config.udpPort);

    // Resolve coordinator host for UDP
    hostent* server = gethostbyname(config.coordinatorHost.c_str());
    if (server) {
        memcpy(&coordAddr.sin_addr.s_addr, server->h_addr, server->h_length);
    } else {
        coordAddr.sin_addr.s_addr = inet_addr(config.coordinatorHost.c_str());
    }

    while (running) {
        // Sample load from /proc/loadavg
        FILE* f = fopen("/proc/loadavg", "r");
        if (f) {
            double l1, l5, l15;
            fscanf(f, "%lf %lf %lf", &l1, &l5, &l15);
            fclose(f);
            hb.load = static_cast<uint32_t>(l1 * 100);
        }

        sendto(udpFd, &hb, sizeof(hb), 0,
               (sockaddr*)&coordAddr, sizeof(coordAddr));

        std::this_thread::sleep_for(std::chrono::seconds(config.heartbeatIntervalSec));
    }
}

void Worker::run() {
    running = true;
    while (running) {
        char buf[4096];
        int n = read(tcpFd, buf, sizeof(buf));
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                continue;
            }
            std::cerr << "[WORKER] TCP read error: " << strerror(errno) << "\n";
            break;
        }
        if (n == 0) {
            std::cerr << "[WORKER] TCP connection closed by coordinator\n";
            break;
        }

        readBuf.append(buf, n);

        // Parse frames
        while (true) {
            TcpFrame frame;
            int consumed = TcpFrame::decode(readBuf.data(), readBuf.size(), frame);
            if (consumed == 0) break;

            readBuf.erase(0, consumed);
            handleMessage(frame.data);
        }
    }
}

void Worker::handleMessage(const std::string& msg) {
    std::string cmd = Proto::parseCmd(msg);

    if (cmd == "task_assign") {
        // Parse task_id, name, cmd
        int taskId = 0;
        auto tPos = msg.find("\"task_id\"");
        if (tPos != std::string::npos) {
            auto col = msg.find(':', tPos + 9);
            auto end = msg.find_first_of(",}", col + 1);
            taskId = std::stoi(msg.substr(col + 1, end - col - 1));
        }

        std::string name, taskCmd;
        auto nPos = msg.find("\"name\"");
        if (nPos != std::string::npos) {
            auto col = msg.find(':', nPos + 6);
            auto q1 = msg.find('"', col + 1);
            auto q2 = msg.find('"', q1 + 1);
            if (q1 != std::string::npos && q2 != std::string::npos)
                name = msg.substr(q1 + 1, q2 - q1 - 1);
        }

        // Find the shell "cmd" field — must be AFTER the "name" field
        auto cmdSearchStart = (nPos != std::string::npos) ? nPos + 6 : 0;
        auto cPos = msg.find("\"cmd\"", cmdSearchStart);
        if (cPos != std::string::npos) {
            auto col = msg.find(':', cPos + 5);
            auto q1 = msg.find('"', col + 1);
            auto q2 = msg.find('"', q1 + 1);
            if (q1 != std::string::npos && q2 != std::string::npos)
                taskCmd = msg.substr(q1 + 1, q2 - q1 - 1);
        }

        runTask(taskId, name, taskCmd);
    } else if (cmd == "heartbeat_ack") {
        // Coordinator acknowledged — can be ignored on worker side
    }
}

void Worker::runTask(int taskId, const std::string& name, const std::string& cmd) {
    std::cerr << "[WORKER] Starting task #" << taskId << ": " << name << "\n";
    std::cerr << "[WORKER] cmd=" << cmd << "\n";

    TaskRunner runner;
    TaskResult result = runner.execute(cmd, config.taskTimeoutSec, config.workDir);

    std::cerr << "[WORKER] Task result: exitCode=" << result.exitCode
              << " dur=" << result.durationSec << "s\n";
    std::cerr << "[WORKER] stdout=\"" << result.stdout.substr(0, 200) << "\"\n";
    std::cerr << "[WORKER] stderr=\"" << result.stderr.substr(0, 200) << "\"\n";

    std::string status = result.exitCode == 0 ? "done" : "failed";
    std::string output = result.exitCode == 0 ? result.stdout : result.stderr;

    // Send result back to coordinator
    std::string msg = Proto::makeTaskResult(taskId, status, result.durationSec, output);
    auto frame = TcpFrame::encode(msg);
    write(tcpFd, frame.data(), frame.size());

    // Push artifacts over TCP
    auto outputsList = QueueSync::getTaskOutputs(config.dbPath, taskId);
    if (!outputsList.empty()) {
        auto files = ArtifactSync::findOutputs(config.workDir, outputsList);
        for (const auto& f : files) {
            std::cerr << "[WORKER] Pushing artifact: " << f << "\n";
            std::string b64 = ArtifactSync::fileToBase64(f);
            auto size = static_cast<int>(b64.size());

            // Send artifact_data frame
            std::string dataMsg = Proto::makeArtifactData(taskId, f, size, b64);
            auto dataFrame = TcpFrame::encode(dataMsg);
            write(tcpFd, dataFrame.data(), dataFrame.size());

            // Wait for ack from coordinator (simple: read next frame)
            // For now, non-blocking sleep; coordinator processes async
        }
    }

    // Update local SQLite as backup
    QueueSync::updateTaskStatus(config.dbPath, taskId,
                                status == "done" ? "DONE" : "FAILED");

    std::cerr << "[WORKER] Task #" << taskId << " done: "
              << status << " (" << result.durationSec << "s)\n";
}

void Worker::stop() {
    running = false;
    if (heartbeatThread.joinable()) heartbeatThread.join();
    if (tcpFd >= 0) close(tcpFd);
    if (udpFd >= 0) close(udpFd);
}
