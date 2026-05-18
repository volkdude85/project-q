#ifndef PROJECTQD_PROTOCOL_H
#define PROJECTQD_PROTOCOL_H

#include <QByteArray>
#include <QJsonObject>
#include <QJsonDocument>
#include <cstdint>

// ─── UDP Heartbeat ────────────────────────────────────────────────────
// Wire format: [magic:4][node_id:16][load:4] = 24 bytes
// magic = 0x50514D00 ("PQM\0")
// node_id = hostname left-padded with spaces, null-terminated
// load = current CPU load * 100 (uint32_t, network byte order)

#pragma pack(push, 1)
struct HeartbeatPacket {
    uint32_t magic;        // 0x50514D00
    char     nodeId[16];   // null-padded hostname
    uint32_t load;         // CPU load * 100
};
#pragma pack(pop)

static_assert(sizeof(HeartbeatPacket) == 24, "HeartbeatPacket must be 24 bytes");

constexpr uint32_t HEARTBEAT_MAGIC = 0x50514D00;

HeartbeatPacket makeHeartbeat(const QString &nodeName, uint32_t load);
bool isValidHeartbeat(const HeartbeatPacket &pkt);
QString heartbeatNodeName(const HeartbeatPacket &pkt);

// ─── TCP Command Protocol ─────────────────────────────────────────────
// Length-prefixed JSON frames:
// [length:4 (uint32_t, network byte order)][JSON payload:length]
//
// Available commands:

enum class TcpCmd {
    Register,
    RegisterAck,
    HeartbeatAck,
    TaskAssign,
    TaskResult,
    TaskResultAck,
    NodeList,
    ArtifactRequest,
    ArtifactChunk,
    ArtifactAck,
    Error,
    Ping,
    Pong
};

QString tcpCmdToString(TcpCmd cmd);
TcpCmd stringToTcpCmd(const QString &str);

// Frame helpers
QByteArray encodeFrame(const QJsonObject &obj);
QJsonObject decodeFrame(const QByteArray &data);

// Command builders
QJsonObject buildRegister(const QString &nodeName, const QStringList &capabilities);
QJsonObject buildRegisterAck(const QString &nodeName, const QString &coordinatorVersion);
QJsonObject buildHeartbeatAck(uint32_t load);
QJsonObject buildTaskAssign(int taskId, const QString &name, const QString &command, int timeoutSec);
QJsonObject buildTaskResult(int taskId, const QString &status, const QStringList &outputs, int durationSec, const QString &errorLog);
QJsonObject buildTaskResultAck(int taskId);
QJsonObject buildNodeList(const QJsonArray &nodes);
QJsonObject buildArtifactRequest(int taskId, const QString &filename);
QJsonObject buildArtifactChunk(int taskId, const QString &filename, int chunkIndex, int totalChunks, const QByteArray &data);
QJsonObject buildArtifactAck(int taskId, const QString &filename);
QJsonObject buildError(const QString &message);
QJsonObject buildPing();
QJsonObject buildPong();

#endif // PROJECTQD_PROTOCOL_H
