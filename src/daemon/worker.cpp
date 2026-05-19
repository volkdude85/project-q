#include "worker.h"
#include "protocol.h"
#include <QJsonDocument>
#include <QDateTime>
#include <QDebug>
#include <QNetworkDatagram>
#include <QtEndian>
#include <QProcess>
#include <QFile>
#include <QDir>

Worker::Worker(const DaemonConfig &config, QObject *parent)
    : QObject(parent), m_config(config) {}

Worker::~Worker() { stop(); }

bool Worker::start() {
    m_tcpSocket = new QTcpSocket(this);
    connect(m_tcpSocket, &QTcpSocket::connected, this, &Worker::onConnected);
    connect(m_tcpSocket, &QTcpSocket::disconnected, this, &Worker::onDisconnected);
    connect(m_tcpSocket, &QTcpSocket::readyRead, this, &Worker::onReadyRead);

    qDebug() << "[WORKER] Connecting to coordinator at" << m_config.coordinatorHost << m_config.coordinatorPort;
    m_tcpSocket->connectToHost(m_config.coordinatorHost, m_config.coordinatorPort);

    // UDP heartbeat sender
    m_udpSocket = new QUdpSocket(this);
    m_heartbeatTimer = new QTimer(this);
    connect(m_heartbeatTimer, &QTimer::timeout, this, &Worker::sendHeartbeat);
    m_heartbeatTimer->start(m_config.heartbeatIntervalSec * 1000);

    // Reconnect timer
    m_reconnectTimer = new QTimer(this);
    connect(m_reconnectTimer, &QTimer::timeout, this, &Worker::reconnect);
    m_reconnectTimer->start(10000); // retry every 10s

    return true;
}

void Worker::stop() {
    if (m_tcpSocket) m_tcpSocket->close();
    if (m_udpSocket) m_udpSocket->close();
    if (m_heartbeatTimer) m_heartbeatTimer->stop();
    if (m_reconnectTimer) m_reconnectTimer->stop();
}

void Worker::onConnected() {
    qDebug() << "[WORKER] Connected to coordinator at" << m_config.coordinatorHost;
    sendRegister();
    emit connected();
}

void Worker::onDisconnected() {
    qDebug() << "[WORKER] Disconnected from coordinator";
    emit disconnected();

    // Kill running tasks on disconnect (they'll be reassigned)
    for (auto it = m_runningTasks.begin(); it != m_runningTasks.end(); ++it) {
        it.value()->kill();
        it.value()->deleteLater();
    }
    m_runningTasks.clear();
}

void Worker::onReadyRead() {
    while (m_tcpSocket->bytesAvailable() >= 4) {
        uint32_t frameLen;
        m_tcpSocket->peek(reinterpret_cast<char*>(&frameLen), 4);
        frameLen = qFromBigEndian<uint32_t>(frameLen);

        if (m_tcpSocket->bytesAvailable() < static_cast<qint64>(4 + frameLen))
            break;

        QByteArray frame = m_tcpSocket->read(4 + frameLen);
        QJsonObject msg = decodeFrame(frame);

        if (!msg.isEmpty()) {
            processCommand(msg);
        }
    }
}

void Worker::processCommand(const QJsonObject &msg) {
    QString cmd = msg["cmd"].toString();

    if (cmd == "task_assign") {
        handleTaskAssign(msg);
    } else if (cmd == "ping") {
        handlePing();
    } else if (cmd == "register_ack") {
        qDebug() << "[WORKER] Registered as" << m_config.nodeName;
    } else if (cmd == "node_list") {
        // Optional: log node list changes
    } else {
        qDebug() << "[WORKER] Received command:" << cmd;
    }
}

void Worker::handleTaskAssign(const QJsonObject &msg) {
    int taskId = msg["task_id"].toInt();
    QString name = msg["name"].toString();
    QString command = msg["command"].toString();
    int timeoutSec = msg["timeout"].toInt();

    qDebug() << "[WORKER] Task assigned: #" << taskId << "(" << name << ")";

    auto *runner = new TaskRunner(taskId, name, command, m_config.workDir, timeoutSec, this);

    connect(runner, &TaskRunner::completed, this, [this, taskId, runner](const QString &status, int duration, const QStringList &outputs, const QString &errorLog, const QString &stdoutContent) {
        QJsonObject result = buildTaskResult(taskId, status, outputs, duration, errorLog);
        if (!stdoutContent.isEmpty())
            result["stdout"] = stdoutContent;
        m_tcpSocket->write(encodeFrame(result));
        m_runningTasks.remove(taskId);
        emit taskCompleted(taskId, status);
        runner->deleteLater();
    });

    m_runningTasks[taskId] = runner;
    emit taskStarted(taskId);
    runner->start();
}

void Worker::handlePing() {
    QJsonObject pong = buildPong();
    m_tcpSocket->write(encodeFrame(pong));
}

void Worker::sendRegister() {
    QStringList caps;
    caps << "x86_64";
    QJsonObject reg = buildRegister(m_config.nodeName, caps);
    m_tcpSocket->write(encodeFrame(reg));
}

void Worker::sendHeartbeat() {
    // Get CPU load via /proc/loadavg
    uint32_t load = 0;
    QFile loadFile("/proc/loadavg");
    if (loadFile.open(QIODevice::ReadOnly)) {
        QByteArray line = loadFile.readLine();
        loadFile.close();
        auto parts = line.split(' ');
        if (parts.size() >= 1) {
            double load1min = QString::fromUtf8(parts[0]).toDouble();
            load = static_cast<uint32_t>(load1min * 100.0);
        }
    }

    HeartbeatPacket pkt = makeHeartbeat(m_config.nodeName, load);
    m_udpSocket->writeDatagram(
        reinterpret_cast<const char*>(&pkt), sizeof(pkt),
        QHostAddress(m_config.coordinatorHost), m_config.udpPort
    );
}

void Worker::reconnect() {
    if (m_tcpSocket->state() != QAbstractSocket::ConnectedState) {
        qDebug() << "[WORKER] Attempting reconnect...";
        m_tcpSocket->connectToHost(m_config.coordinatorHost, m_config.coordinatorPort);
    }
}
