#include "config.h"
#include <QCommandLineParser>
#include <QDir>
#include <QStandardPaths>
#include <QHostInfo>
#include <QDebug>

DaemonConfig DaemonConfig::fromCommandLine(QCoreApplication &app) {
    QCommandLineParser parser;
    parser.setApplicationDescription("Project-Qd: Distributed compile farm daemon");

    // Mode
    parser.addOption(QCommandLineOption({"c", "coordinator"}, "Run as coordinator"));
    parser.addOption(QCommandLineOption({"w", "worker"}, "Run as worker"));

    // Network
    parser.addOption(QCommandLineOption("bind", "Bind address", "addr", "0.0.0.0"));
    parser.addOption(QCommandLineOption("tcp-port", "TCP command port", "port", "9100"));
    parser.addOption(QCommandLineOption("udp-port", "UDP heartbeat port", "port", "9101"));

    // Coordinator connection (worker mode)
    parser.addOption(QCommandLineOption("connect", "Coordinator address:port", "host:port"));

    // Identity
    parser.addOption(QCommandLineOption("node-name", "Node name (default: hostname)", "name"));

    // Paths
    parser.addOption(QCommandLineOption("data-dir", "Data directory", "path"));
    parser.addOption(QCommandLineOption("queue-db", "Queue database path", "path"));
    parser.addOption(QCommandLineOption("work-dir", "Working directory for tasks", "path"));

    // Heartbeat
    parser.addOption(QCommandLineOption("hb-interval", "Heartbeat interval (sec)", "sec", "5"));
    parser.addOption(QCommandLineOption("hb-timeout", "Heartbeat timeout (sec)", "sec", "20"));

    // Task limits
    parser.addOption(QCommandLineOption("max-tasks", "Max concurrent tasks", "n", "4"));
    parser.addOption(QCommandLineOption("task-timeout", "Default task timeout (sec)", "sec", "1800"));

    parser.addHelpOption();
    parser.addVersionOption();
    parser.process(app);

    DaemonConfig cfg;

    // Mode detection
    cfg.isCoordinator = parser.isSet("coordinator");
    bool isWorker = parser.isSet("worker");

    if (cfg.isCoordinator && isWorker) {
        qFatal("Cannot run as both coordinator and worker");
    }
    if (!cfg.isCoordinator && !isWorker) {
        qFatal("Must specify --coordinator or --worker");
    }

    // Network
    cfg.bindAddress = QHostAddress(parser.value("bind"));
    cfg.tcpPort = parser.value("tcp-port").toUShort();
    cfg.udpPort = parser.value("udp-port").toUShort();

    // Coordinator address
    if (parser.isSet("connect")) {
        QString conn = parser.value("connect");
        int colon = conn.lastIndexOf(':');
        if (colon > 0) {
            cfg.coordinatorHost = conn.left(colon);
            cfg.coordinatorPort = conn.mid(colon + 1).toUShort();
        } else {
            cfg.coordinatorHost = conn;
        }
    }

    // Node name
    cfg.nodeName = parser.isSet("node-name")
        ? parser.value("node-name")
        : QHostInfo::localHostName();

    // Data paths
    QString defaultDataDir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    cfg.dataDir = parser.isSet("data-dir") ? parser.value("data-dir") : defaultDataDir;
    cfg.queueDbPath = parser.isSet("queue-db")
        ? parser.value("queue-db")
        : cfg.dataDir + "/queue.db";
    cfg.workDir = parser.isSet("work-dir")
        ? parser.value("work-dir")
        : cfg.dataDir + "/work";

    // Ensure directories exist
    QDir().mkpath(cfg.dataDir);
    QDir().mkpath(cfg.workDir);

    // Heartbeat
    cfg.heartbeatIntervalSec = parser.value("hb-interval").toInt();
    cfg.heartbeatTimeoutSec = parser.value("hb-timeout").toInt();

    // Task limits
    cfg.maxConcurrentTasks = parser.value("max-tasks").toInt();
    cfg.defaultTaskTimeoutSec = parser.value("task-timeout").toInt();

    return cfg;
}

void DaemonConfig::dump() const {
    qDebug() << "── Project-Qd Config ──";
    qDebug() << "Mode:" << (isCoordinator ? "COORDINATOR" : "WORKER");
    qDebug() << "Node:" << nodeName;
    qDebug() << "TCP:" << bindAddress.toString() << tcpPort;
    qDebug() << "UDP:" << bindAddress.toString() << udpPort;
    if (!isCoordinator) {
        qDebug() << "Coordinator:" << coordinatorHost << coordinatorPort;
    }
    qDebug() << "Data dir:" << dataDir;
    qDebug() << "Queue DB:" << queueDbPath;
    qDebug() << "Work dir:" << workDir;
    qDebug() << "Heartbeat interval:" << heartbeatIntervalSec << "s";
    qDebug() << "Heartbeat timeout:" << heartbeatTimeoutSec << "s";
    qDebug() << "Max tasks:" << maxConcurrentTasks;
    qDebug() << "Task timeout:" << defaultTaskTimeoutSec << "s";
    qDebug() << "──────────────────────";
}
