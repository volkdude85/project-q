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

    QString nodeName = m_socketToNode.value(socket);

    qDebug() << "[COORD] Task #" << taskId << "from" << nodeName << ":" << status << "(" << durationSec << "s)";

    // Update task record
    for (auto &task : m_taskQueue) {
        if (task.id == taskId) {
            task.status = status;
            task.durationSec = durationSec;
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

    // Find a node with capacity
    for (auto it = m_nodes.begin(); it != m_nodes.end(); ++it) {
        if (it->activeTasks < m_config.maxConcurrentTasks) {
            QString nodeName = it.key();
            task.status = "running";
            task.assignedNode = nodeName;
            task.startedMs = QDateTime::currentMSecsSinceEpoch();
            it->activeTasks++;

            QJsonObject assign = buildTaskAssign(task.id, task.name, task.command, task.timeoutSec);
            sendToWorker(nodeName, assign);

            qDebug() << "[COORD] Dispatched task #" << task.id << "(" << task.name << ") to" << nodeName;
            emit taskStarted(task.id, nodeName);
            broadcastNodeList();
            break;
        }
    }
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
