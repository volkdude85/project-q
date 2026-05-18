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
    uint32_t lastLoad = 0;
    int activeTasks = 0;
};

struct TaskRecord {
    int id;
    QString name;
    QString command;
    QString assignedNode;
    QString status; // pending, running, done, failed
    int durationSec = 0;
    qint64 startedMs = 0;
    int timeoutSec = 1800;
};

class Coordinator : public QObject {
    Q_OBJECT
public:
    explicit Coordinator(const DaemonConfig &config, QObject *parent = nullptr);
    ~Coordinator();

    bool start();
    void stop();

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
    void sendToWorker(const QString &nodeName, const QJsonObject &msg);
    void broadcastNodeList();

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
