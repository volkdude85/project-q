#ifndef PROJECTQD_COORDINATOR_H
#define PROJECTQD_COORDINATOR_H

#include <QObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUdpSocket>
#include <QTimer>
#include <QHash>
#include <QSet>
#include "config.h"
#include "protocol.h"

struct NodeInfo {
    QString name;
    QStringList capabilities;
    QHostAddress address;
    quint16 tcpPort;
    qint64 lastHeartbeatMs = 0;
    qint64 registeredAtMs = 0;   // when register command was processed
    bool fullyRegistered = false; // false during grace window
    uint32_t lastLoad = 0;
    int activeTasks = 0;
};

struct TaskRecord {
    int id;
    QString name;
    QString command;
    QString assignedNode;
    QString targetNode;   // specific node to route to, empty = round-robin
    QString status; // pending, running, done, failed
    int durationSec = 0;
    qint64 startedMs = 0;
    int timeoutSec = 1800;
    QString stdoutContent;  // captured from worker result
    QString errorLog;
};

class Coordinator : public QObject {
    Q_OBJECT
public:
    explicit Coordinator(const DaemonConfig &config, QObject *parent = nullptr);
    ~Coordinator();

    bool start();
    void stop();

    int addTask(const QString &name, const QString &command, int timeoutSec = 1800, const QString &targetNode = "");

signals:
    void nodeRegistered(const QString &name);
    void nodeDisconnected(const QString &name);
    void taskQueued(int taskId);
    void taskStarted(int taskId, const QString &node);
    void taskCompleted(int taskId, const QString &status);

private slots:
    void onNewTcpConnection();
    void onTcpReadyRead(QTcpSocket *socket);
    void onUdpReadyRead();
    void checkHeartbeatTimeouts();
    void dispatchNextTask();

private:
    void processCommand(QTcpSocket *socket, const QJsonObject &msg);
    void handleRegister(QTcpSocket *socket, const QJsonObject &msg);
    void handleTaskResult(QTcpSocket *socket, const QJsonObject &msg);
    void handlePing(QTcpSocket *socket);
    void assignTask(TaskRecord &task, const QString &nodeName);
    void sendToWorker(const QString &nodeName, const QJsonObject &msg);
    void broadcastNodeList();

    /* ─── Registration grace window ─────────────────── */
    // Newly registered nodes get this many seconds before heartbeat timeouts apply
    static constexpr qint64 REGISTRATION_GRACE_MS = 60 * 1000;

    // ─── TCP Heartbeat handler ────────────────────────
    // Workers that send TCP heartbeats before finishing registration
    // are tracked via peer address + port so the first few heartbeats
    // don't get silently dropped.
    //
    // Worker-side: sends heartbeat before register completes
    // Coordinator-side: holds pending heartbeats keyed by socket,
    //   applies them when the node registers.
    QHash<QTcpSocket*, NodeInfo> m_pendingRegistrations;

    // Round-robin dispatch cursor — don't always start at node 0
    int m_dispatchCursor = 0;
    DaemonConfig m_config;
    QTcpServer *m_tcpServer = nullptr;
    QUdpSocket *m_udpSocket = nullptr;
    QTimer *m_heartbeatTimer = nullptr;
    QTimer *m_dispatchTimer = nullptr;

    QHash<QString, NodeInfo> m_nodes;
    QHash<QTcpSocket*, QString> m_socketToNode; // reverse lookup
    QList<TaskRecord> m_taskQueue;
    int m_nextTaskId = 1;
};

#endif
