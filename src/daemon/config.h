#ifndef PROJECTQD_CONFIG_H
#define PROJECTQD_CONFIG_H

#include <QString>
#include <QHostAddress>
#include <QCommandLineParser>
#include <QCoreApplication>

struct DaemonConfig {
    // Mode
    bool isCoordinator = false;

    // Network
    QHostAddress bindAddress = QHostAddress::Any;
    quint16 tcpPort = 9100;
    quint16 udpPort = 9101;

    // Coordinator address (for workers)
    QString coordinatorHost;
    quint16 coordinatorPort = 9100;

    // Node identity
    QString nodeName;

    // Heartbeat
    int heartbeatIntervalSec = 5;
    int heartbeatTimeoutSec = 20;

    // Task limits
    int maxConcurrentTasks = 4;
    int defaultTaskTimeoutSec = 1800;

    // Data paths
    QString dataDir;
    QString queueDbPath;
    QString workDir;

    static DaemonConfig fromCommandLine(QCoreApplication &app);
    void dump() const;
};

#endif // PROJECTQD_CONFIG_H
