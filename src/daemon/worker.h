#ifndef PROJECTQD_WORKER_H
#define PROJECTQD_WORKER_H

#include <QObject>
#include <QTcpSocket>
#include <QUdpSocket>
#include <QTimer>
#include "config.h"
#include "taskrunner.h"

class Worker : public QObject {
    Q_OBJECT
public:
    explicit Worker(const DaemonConfig &config, QObject *parent = nullptr);
    ~Worker();

    bool start();
    void stop();

signals:
    void connected();
    void disconnected();
    void taskStarted(int taskId);
    void taskCompleted(int taskId, const QString &status);

private slots:
    void onConnected();
    void onDisconnected();
    void onReadyRead();
    void sendHeartbeat();
    void reconnect();

private:
    void processCommand(const QJsonObject &msg);
    void handleTaskAssign(const QJsonObject &msg);
    void handlePing();
    void sendRegister();

    DaemonConfig m_config;
    QTcpSocket *m_tcpSocket = nullptr;
    QUdpSocket *m_udpSocket = nullptr;
    QTimer *m_heartbeatTimer = nullptr;
    QTimer *m_reconnectTimer = nullptr;
    QHash<int, TaskRunner*> m_runningTasks;
};

#endif
