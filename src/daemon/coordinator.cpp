#include "coordinator.h"
#include "queue_sync.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QDateTime>
#include <QDebug>
#include <QNetworkDatagram>
#include <QtEndian>

Coordinator::Coordinator(const DaemonConfig &config, QObject *parent)
    : QObject(parent), m_config(config) {}

Coordinator::~Coordinator() { stop(); }

bool Coordinator::start() {
    // TCP server
    m_tcpServer = new QTcpServer(this);
    connect(m_tcpServer, &QTcpServer::newConnection, this, &Coordinator::onNewTcpConnection);

    if (!m_tcpServer->listen(m_config.bindAddress, m_config.tcpPort)) {
        qCritical() << "[COORD] Failed to bind TCP" << m_config.tcpPort << m_tcpServer->errorString();
        return false;
    }
    qDebug() << "[COORD] TCP listening on" << m_config.tcpPort;

    // UDP socket
    m_udpSocket = new QUdpSocket(this);
    connect(m_udpSocket, &QUdpSocket::readyRead, this, &Coordinator::onUdpReadyRead);

    if (!m_udpSocket->bind(m_config.bindAddress, m_config.udpPort)) {
        qCritical() << "[COORD] Failed to bind UDP" << m_config.udpPort << m_udpSocket->errorString();
        return false;
    }
    qDebug() << "[COORD] UDP listening on" << m_config.udpPort;

    // Heartbeat timeout checker
    m_heartbeatTimer = new QTimer(this);
    connect(m_heartbeatTimer, &QTimer::timeout, this, &Coordinator::checkHeartbeatTimeouts);
    m_heartbeatTimer->start(5000); // check every 5s

    // Task dispatch tick
    m_dispatchTimer = new QTimer(this);
    connect(m_dispatchTimer, &QTimer::timeout, this, &Coordinator::dispatchNextTask);
    m_dispatchTimer->start(2000); // check every 2s

    return true;
}

void Coordinator::stop() {
    if (m_tcpServer) m_tcpServer->close();
    if (m_udpSocket) m_udpSocket->close();
}

// ─── TCP Connections ──────────────────────────────────────────────────

void Coordinator::onNewTcpConnection() {
    while (auto *socket = m_tcpServer->nextPendingConnection()) {
        connect(socket, &QTcpSocket::readyRead, this, [this, socket]() { onTcpReadyRead(socket); });
        connect(socket, &QTcpSocket::disconnected, this, [this, socket]() {
            // If this socket belongs to a registered node, remove it
            if (m_socketToNode.contains(socket)) {
                QString name = m_socketToNode.take(socket);
                if (m_nodes.contains(name)) {
                    qDebug() << "[COORD] Node disconnected (TCP):" << name;
                    m_nodes.remove(name);
                    emit nodeDisconnected(name);
                    broadcastNodeList();
                }
            }
            socket->deleteLater();
        });
        qDebug() << "[COORD] New TCP connection from" << socket->peerAddress().toString();
    }
}

void Coordinator::onTcpReadyRead(QTcpSocket *socket) {
    // Read length-prefixed frames
    while (socket->bytesAvailable() >= 4) {
        uint32_t frameLen;
        socket->peek(reinterpret_cast<char*>(&frameLen), 4);
        frameLen = qFromBigEndian<uint32_t>(frameLen);

        if (socket->bytesAvailable() < static_cast<qint64>(4 + frameLen))
            break; // wait for more data

        QByteArray frame = socket->read(4 + frameLen);
        QJsonObject msg = decodeFrame(frame);

        if (msg.isEmpty()) {
            qWarning() << "[COORD] Empty or invalid frame from" << socket->peerAddress().toString();
            continue;
        }

        processCommand(socket, msg);
    }
}

void Coordinator::processCommand(QTcpSocket *socket, const QJsonObject &msg) {
    QString cmd = msg["cmd"].toString();

    if (cmd == "register") {
        handleRegister(socket, msg);
    } else if (cmd == "task_result") {
        handleTaskResult(socket, msg);
    } else if (cmd == "ping") {
        handlePing(socket);
    } else if (cmd == "submit") {
        QString name = msg["name"].toString();
        QString command = msg["command"].toString();
        QString targetNode = msg.value("target_node").toString();
        int timeout = msg.value("timeout_sec").toInt(1800);
        if (name.isEmpty() || command.isEmpty()) {
            socket->write(encodeFrame(buildError("submit requires 'name' and 'command'")));
            return;
        }
        int id = addTask(name, command, timeout, targetNode);
        QJsonObject ack;
        ack["cmd"] = "submit_ack";
        ack["task_id"] = id;
        socket->write(encodeFrame(ack));
    } else if (cmd == "heartbeat") {
        // TCP heartbeat fallback — updates node liveness when UDP is blocked
        QString nodeName = m_socketToNode.value(socket);
        if (!nodeName.isEmpty() && m_nodes.contains(nodeName)) {
            m_nodes[nodeName].lastHeartbeatMs = QDateTime::currentMSecsSinceEpoch();
            m_nodes[nodeName].lastLoad = msg.value("load").toVariant().toUInt();
            // After the first TCP heartbeat, mark fully registered
            m_nodes[nodeName].fullyRegistered = true;
            QJsonObject ack;
            ack["cmd"] = "heartbeat_ack";
            ack["load"] = msg["load"];
            socket->write(encodeFrame(ack));
        } else if (m_socketToNode.contains(socket)) {
            // Socket is mapped but node hasn't been added yet — race window
            // This shouldn't happen, but log it
            qDebug() << "[COORD] Heartbeat from mapped but unregistered socket";
        } else {
            // Heartbeat arrived before register command — store as pending
            NodeInfo pending;
            pending.lastHeartbeatMs = QDateTime::currentMSecsSinceEpoch();
            pending.lastLoad = msg.value("load").toVariant().toUInt();
            pending.fullyRegistered = true;
            m_pendingRegistrations[socket] = pending;
            qDebug() << "[COORD] Stored pending heartbeat for unregistered worker at"
                     << socket->peerAddress().toString();
        }
    } else if (cmd == "list_nodes") {
        // Dump all registered nodes
        QJsonObject result;
        result["cmd"] = "node_list_response";
        QJsonArray nodesArr;
        for (auto it = m_nodes.begin(); it != m_nodes.end(); ++it) {
            QJsonObject n;
            n["name"] = it->name;
            n["load"] = static_cast<double>(it->lastLoad);
            n["active_tasks"] = it->activeTasks;
            n["last_heartbeat_sec_ago"] = (QDateTime::currentMSecsSinceEpoch() - it->lastHeartbeatMs) / 1000;
            n["address"] = it->address.toString();
            n["registered_sec_ago"] = (QDateTime::currentMSecsSinceEpoch() - it->registeredAtMs) / 1000;
            n["fully_registered"] = it->fullyRegistered || it->registeredAtMs > 0;
            nodesArr.append(n);
        }
        result["nodes"] = nodesArr;
        result["count"] = nodesArr.size();
        socket->write(encodeFrame(result));
    } else if (cmd == "task_query") {
        int taskId = msg["task_id"].toInt();
        QJsonObject result;
        result["cmd"] = "task_query_result";
        result["task_id"] = taskId;
        result["found"] = false;
        for (const auto &t : m_taskQueue) {
            if (t.id == taskId) {
                result["found"] = true;
                result["status"] = t.status;
                result["name"] = t.name;
                result["assigned_node"] = t.assignedNode;
                result["duration_sec"] = t.durationSec;
                if (!t.stdoutContent.isEmpty())
                    result["stdout"] = t.stdoutContent;
                if (!t.errorLog.isEmpty())
                    result["error_log"] = t.errorLog;
                break;
            }
        }
        socket->write(encodeFrame(result));
    } else {
        qWarning() << "[COORD] Unknown command:" << cmd;
        QJsonObject err = buildError("Unknown command: " + cmd);
        socket->write(encodeFrame(err));
    }
}

void Coordinator::handleRegister(QTcpSocket *socket, const QJsonObject &msg) {
    QString name = msg["node"].toString();
    if (name.isEmpty()) {
        socket->write(encodeFrame(buildError("Register requires 'node' field")));
        return;
    }

    QJsonArray capsArray = msg["capabilities"].toArray();
    QStringList capabilities;
    for (const auto &c : capsArray) capabilities.append(c.toString());

    NodeInfo info;
    info.name = name;
    info.capabilities = capabilities;
    info.address = socket->peerAddress();
    info.tcpPort = m_config.tcpPort;
    info.lastHeartbeatMs = QDateTime::currentMSecsSinceEpoch();
    info.registeredAtMs = QDateTime::currentMSecsSinceEpoch();
    info.fullyRegistered = false;  // grace period starts now
    info.activeTasks = 0;

    // If node re-registers, close old socket
    if (m_nodes.contains(name)) {
        qDebug() << "[COORD] Node re-registered:" << name;
    } else {
        qDebug() << "[COORD] Node registered:" << name << "caps:" << capabilities;
    }

    // Remove old socket mapping if exists
    auto oldIt = std::find_if(m_socketToNode.begin(), m_socketToNode.end(),
        [&name](const QString &n) { return n == name; });
    if (oldIt != m_socketToNode.end()) {
        m_socketToNode.remove(oldIt.key());
    }

    m_nodes[name] = info;
    m_socketToNode[socket] = name;

    // Apply any pending heartbeats that arrived before registration completed
    if (m_pendingRegistrations.contains(socket)) {
        NodeInfo &pending = m_pendingRegistrations[socket];
        m_nodes[name].lastHeartbeatMs = pending.lastHeartbeatMs;
        m_nodes[name].lastLoad = pending.lastLoad;
        m_nodes[name].fullyRegistered = true;
        m_pendingRegistrations.remove(socket);
        qDebug() << "[COORD] Applied pre-registration heartbeat for" << name;
    }

    // Send registration acknowledgment
    QJsonObject ack = buildRegisterAck(name, "1.0.0");
    socket->write(encodeFrame(ack));

    emit nodeRegistered(name);
    broadcastNodeList();
}

void Coordinator::handleTaskResult(QTcpSocket *socket, const QJsonObject &msg) {
    int taskId = msg["task_id"].toInt();
    QString status = msg["status"].toString();
    int durationSec = msg["duration_sec"].toInt();
    QString stdoutContent = msg.value("stdout").toString();
    QString errorLog = msg.value("error_log").toString();

    QString nodeName = m_socketToNode.value(socket);

    qDebug() << "[COORD] Task #" << taskId << "from" << nodeName << ":" << status << "(" << durationSec << "s)";

    // Update task record
    for (auto &task : m_taskQueue) {
        if (task.id == taskId) {
            task.status = status;
            task.durationSec = durationSec;
            task.stdoutContent = stdoutContent;
            task.errorLog = errorLog;
            break;
        }
    }

    // Reduce active task count for node
    if (m_nodes.contains(nodeName)) {
        m_nodes[nodeName].activeTasks = qMax(0, m_nodes[nodeName].activeTasks - 1);
    }

    // Acknowledge
    QJsonObject ack = buildTaskResultAck(taskId);
    socket->write(encodeFrame(ack));

    emit taskCompleted(taskId, status);
    broadcastNodeList();
}

void Coordinator::handlePing(QTcpSocket *socket) {
    QJsonObject pong = buildPong();
    socket->write(encodeFrame(pong));
}

// ─── UDP Heartbeat Handling ───────────────────────────────────────────

void Coordinator::onUdpReadyRead() {
    while (m_udpSocket->hasPendingDatagrams()) {
        QNetworkDatagram datagram = m_udpSocket->receiveDatagram();
        QByteArray data = datagram.data();

        if (data.size() != sizeof(HeartbeatPacket)) continue;

        HeartbeatPacket pkt;
        std::memcpy(&pkt, data.constData(), sizeof(pkt));

        if (!isValidHeartbeat(pkt)) continue;

        QString nodeName = heartbeatNodeName(pkt);
        uint32_t load = qFromBigEndian<uint32_t>(pkt.load);

        if (m_nodes.contains(nodeName)) {
            m_nodes[nodeName].lastHeartbeatMs = QDateTime::currentMSecsSinceEpoch();
            m_nodes[nodeName].lastLoad = load;
        }
    }
}

void Coordinator::checkHeartbeatTimeouts() {
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    qint64 timeoutMs = m_config.heartbeatTimeoutSec * 1000;

    QStringList timedOut;
    for (auto it = m_nodes.begin(); it != m_nodes.end(); ++it) {
        // Grace period: newly registered nodes get a window before timeouts apply
        qint64 ageMs = now - it->registeredAtMs;
        if (ageMs < REGISTRATION_GRACE_MS) {
            // Still in grace window — skip timeout check but log progress
            if (!it->fullyRegistered && (ageMs % 10000) < 5000) {
                qDebug() << "[COORD] Grace period for" << it->name << ageMs/1000 << "s elapsed, waiting for first heartbeat";
            }
            continue;
        }

        // After grace period — normal timeout check
        if (now - it->lastHeartbeatMs > timeoutMs) {
            timedOut.append(it.key());
        }
    }

    for (const auto &name : timedOut) {
        qDebug() << "[COORD] Node heartbeat timeout:" << name;
        m_nodes.remove(name);
        emit nodeDisconnected(name);
    }

    if (!timedOut.isEmpty()) {
        broadcastNodeList();
    }
}

// ─── Task Management ────────────────────────────────────────────────

int Coordinator::addTask(const QString &name, const QString &command, int timeoutSec, const QString &targetNode) {
    int id = m_nextTaskId++;
    TaskRecord t;
    t.id = id;
    t.name = name;
    t.command = command;
    t.timeoutSec = timeoutSec;
    t.targetNode = targetNode;
    t.status = "pending";
    m_taskQueue.append(t);
    qDebug() << "[COORD] Queued task #" << id << ":" << name << (targetNode.isEmpty() ? "" : "→"+targetNode);
    emit taskQueued(id);
    return id;
}

// ─── Task Dispatch ────────────────────────────────────────────────────

void Coordinator::dispatchNextTask() {
    // Find a pending task
    int pendingIdx = -1;
    for (int i = 0; i < m_taskQueue.size(); ++i) {
        if (m_taskQueue[i].status == "pending" || m_taskQueue[i].status == "queued") {
            pendingIdx = i;
            break;
        }
    }

    if (pendingIdx < 0) return;
    TaskRecord &task = m_taskQueue[pendingIdx];

    int nodeCount = m_nodes.size();
    if (nodeCount == 0) return;

    // Collect node names in order
    QStringList nodeNames;
    for (auto it = m_nodes.begin(); it != m_nodes.end(); ++it)
        nodeNames.append(it.key());
    nodeNames.sort();

    // If task has a target node, try it first
    if (!task.targetNode.isEmpty()) {
        auto it = m_nodes.find(task.targetNode);
        if (it != m_nodes.end() && it->activeTasks < m_config.maxConcurrentTasks) {
            assignTask(task, task.targetNode);
            return;
        }
        // Target node doesn't exist or is full — fall through to round-robin
    }

    // Round-robin dispatch
    for (int i = 0; i < nodeCount; ++i) {
        int idx = (m_dispatchCursor + i) % nodeCount;
        QString nodeName = nodeNames[idx];
        auto it = m_nodes.find(nodeName);
        if (it == m_nodes.end()) continue;

        if (it->activeTasks < m_config.maxConcurrentTasks) {
            assignTask(task, nodeName);
            m_dispatchCursor = (idx + 1) % nodeCount;
            return;
        }
    }
    // All nodes full — task stays pending
}

void Coordinator::assignTask(TaskRecord &task, const QString &nodeName) {
    task.status = "running";
    task.assignedNode = nodeName;
    task.startedMs = QDateTime::currentMSecsSinceEpoch();
    m_nodes[nodeName].activeTasks++;

    QJsonObject assign = buildTaskAssign(task.id, task.name, task.command, task.timeoutSec);
    sendToWorker(nodeName, assign);

    qDebug() << "[COORD] Dispatched task #" << task.id << "(" << task.name << ") to" << nodeName;
    emit taskStarted(task.id, nodeName);
    broadcastNodeList();
}

void Coordinator::sendToWorker(const QString &nodeName, const QJsonObject &msg) {
    // Find the socket for this node
    for (auto it = m_socketToNode.begin(); it != m_socketToNode.end(); ++it) {
        if (it.value() == nodeName) {
            it.key()->write(encodeFrame(msg));
            break;
        }
    }
}

void Coordinator::broadcastNodeList() {
    QJsonArray nodesJson;
    for (auto it = m_nodes.begin(); it != m_nodes.end(); ++it) {
        QJsonObject n;
        n["name"] = it->name;
        n["load"] = static_cast<double>(it->lastLoad);
        n["active_tasks"] = it->activeTasks;
        n["fully_registered"] = it->fullyRegistered;
        n["capabilities"] = QJsonArray::fromStringList(it->capabilities);
        nodesJson.append(n);
    }

    QJsonObject msg = buildNodeList(nodesJson);
    for (auto it = m_socketToNode.begin(); it != m_socketToNode.end(); ++it) {
        it.key()->write(encodeFrame(msg));
    }
}

// Public API for adding tasks (called by QueueSync or CLI)
// Signal implementation is auto-generated by moc
