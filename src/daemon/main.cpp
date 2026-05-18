#include "config.h"
#include "coordinator.h"
#include "worker.h"
#include "queue_sync.h"
#include <QCoreApplication>
#include <QDebug>
#include <QTimer>

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName("project-qd");
    QCoreApplication::setApplicationVersion("1.0.0");

    DaemonConfig cfg = DaemonConfig::fromCommandLine(app);
    cfg.dump();

    if (cfg.runGc) {
        auto *coordinator = new Coordinator(cfg, &app);
        auto *queueSync = new QueueSync(cfg, coordinator, &app);
        if (!queueSync->open()) {
            qCritical() << "[QD] Cannot open queue database for GC";
            return 1;
        }
        int purged = queueSync->purgeOldTasks(cfg.retentionDays);
        qDebug() << "[QD] GC complete: purged" << purged << "tasks";
        return 0;
    }

    if (cfg.isCoordinator) {
        auto *coordinator = new Coordinator(cfg, &app);

        if (!coordinator->start()) {
            qCritical() << "[QD] Failed to start coordinator";
            return 1;
        }

        auto *queueSync = new QueueSync(cfg, coordinator, &app);
        if (!queueSync->open()) {
            qWarning() << "[QD] Queue database unavailable — running without persistence";
        }

        qDebug() << "[QD] Coordinator ready — waiting for workers on TCP" << cfg.tcpPort << "UDP" << cfg.udpPort;
    } else {
        auto *worker = new Worker(cfg, &app);

        if (!worker->start()) {
            qCritical() << "[QD] Failed to start worker";
            return 1;
        }

        qDebug() << "[QD] Worker started — connecting to" << cfg.coordinatorHost << cfg.coordinatorPort;
    }

    return app.exec();
}
