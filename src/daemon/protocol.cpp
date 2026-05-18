#include "protocol.h"
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>
#include <QByteArray>
#include <cstring>
#include <QtEndian>

// ─── UDP Heartbeat ────────────────────────────────────────────────────

HeartbeatPacket makeHeartbeat(const QString &nodeName, uint32_t load) {
    HeartbeatPacket pkt{};
    pkt.magic = qToBigEndian<uint32_t>(HEARTBEAT_MAGIC);
    QByteArray nameBytes = nodeName.toUtf8().left(15);
    std::memcpy(pkt.nodeId, nameBytes.constData(), nameBytes.size());
    pkt.nodeId[nameBytes.size()] = '\0';
    pkt.load = qToBigEndian<uint32_t>(load);
    return pkt;
}

bool isValidHeartbeat(const HeartbeatPacket &pkt) {
    return qFromBigEndian<uint32_t>(pkt.magic) == HEARTBEAT_MAGIC;
}

QString heartbeatNodeName(const HeartbeatPacket &pkt) {
    return QString::fromUtf8(pkt.nodeId, strnlen(pkt.nodeId, 16));
}

// ─── TCP Command Protocol ─────────────────────────────────────────────

QString tcpCmdToString(TcpCmd cmd) {
    switch (cmd) {
        case TcpCmd::Register: return "register";
        case TcpCmd::RegisterAck: return "register_ack";
        case TcpCmd::HeartbeatAck: return "heartbeat_ack";
        case TcpCmd::TaskAssign: return "task_assign";
        case TcpCmd::TaskResult: return "task_result";
        case TcpCmd::TaskResultAck: return "task_result_ack";
        case TcpCmd::NodeList: return "node_list";
        case TcpCmd::ArtifactRequest: return "artifact_request";
        case TcpCmd::ArtifactChunk: return "artifact_chunk";
        case TcpCmd::ArtifactAck: return "artifact_ack";
        case TcpCmd::Error: return "error";
        case TcpCmd::Ping: return "ping";
        case TcpCmd::Pong: return "pong";
    }
    return "unknown";
}

TcpCmd stringToTcpCmd(const QString &str) {
    if (str == "register") return TcpCmd::Register;
    if (str == "register_ack") return TcpCmd::RegisterAck;
    if (str == "heartbeat_ack") return TcpCmd::HeartbeatAck;
    if (str == "task_assign") return TcpCmd::TaskAssign;
    if (str == "task_result") return TcpCmd::TaskResult;
    if (str == "task_result_ack") return TcpCmd::TaskResultAck;
    if (str == "node_list") return TcpCmd::NodeList;
    if (str == "artifact_request") return TcpCmd::ArtifactRequest;
    if (str == "artifact_chunk") return TcpCmd::ArtifactChunk;
    if (str == "artifact_ack") return TcpCmd::ArtifactAck;
    if (str == "error") return TcpCmd::Error;
    if (str == "ping") return TcpCmd::Ping;
    if (str == "pong") return TcpCmd::Pong;
    return TcpCmd::Error;
}

QByteArray encodeFrame(const QJsonObject &obj) {
    QByteArray json = QJsonDocument(obj).toJson(QJsonDocument::Compact);
    uint32_t len = qToBigEndian<uint32_t>(json.size());
    QByteArray frame;
    frame.append(reinterpret_cast<const char*>(&len), 4);
    frame.append(json);
    return frame;
}

QJsonObject decodeFrame(const QByteArray &data) {
    if (data.size() < 4) return {};
    QJsonDocument doc = QJsonDocument::fromJson(data.mid(4));
    return doc.object();
}

// ─── Command Builders ─────────────────────────────────────────────────

QJsonObject buildRegister(const QString &nodeName, const QStringList &capabilities) {
    QJsonObject msg;
    msg["cmd"] = tcpCmdToString(TcpCmd::Register);
    msg["node"] = nodeName;
    QJsonArray caps;
    for (const auto &c : capabilities) caps.append(c);
    msg["capabilities"] = caps;
    return msg;
}

QJsonObject buildRegisterAck(const QString &nodeName, const QString &coordinatorVersion) {
    QJsonObject msg;
    msg["cmd"] = tcpCmdToString(TcpCmd::RegisterAck);
    msg["node"] = nodeName;
    msg["version"] = coordinatorVersion;
    return msg;
}

QJsonObject buildHeartbeatAck(uint32_t load) {
    QJsonObject msg;
    msg["cmd"] = tcpCmdToString(TcpCmd::HeartbeatAck);
    msg["load"] = static_cast<double>(load);
    return msg;
}

QJsonObject buildTaskAssign(int taskId, const QString &name, const QString &command, int timeoutSec) {
    QJsonObject msg;
    msg["cmd"] = tcpCmdToString(TcpCmd::TaskAssign);
    msg["task_id"] = taskId;
    msg["name"] = name;
    msg["command"] = command;
    msg["timeout"] = timeoutSec;
    return msg;
}

QJsonObject buildTaskResult(int taskId, const QString &status, const QStringList &outputs, int durationSec, const QString &errorLog) {
    QJsonObject msg;
    msg["cmd"] = tcpCmdToString(TcpCmd::TaskResult);
    msg["task_id"] = taskId;
    msg["status"] = status;
    QJsonArray outs;
    for (const auto &o : outputs) outs.append(o);
    msg["outputs"] = outs;
    msg["duration_sec"] = durationSec;
    if (!errorLog.isEmpty()) msg["error_log"] = errorLog;
    return msg;
}

QJsonObject buildTaskResultAck(int taskId) {
    QJsonObject msg;
    msg["cmd"] = tcpCmdToString(TcpCmd::TaskResultAck);
    msg["task_id"] = taskId;
    return msg;
}

QJsonObject buildNodeList(const QJsonArray &nodes) {
    QJsonObject msg;
    msg["cmd"] = tcpCmdToString(TcpCmd::NodeList);
    msg["nodes"] = nodes;
    return msg;
}

QJsonObject buildArtifactRequest(int taskId, const QString &filename) {
    QJsonObject msg;
    msg["cmd"] = tcpCmdToString(TcpCmd::ArtifactRequest);
    msg["task_id"] = taskId;
    msg["filename"] = filename;
    return msg;
}

QJsonObject buildArtifactChunk(int taskId, const QString &filename, int chunkIndex, int totalChunks, const QByteArray &data) {
    QJsonObject msg;
    msg["cmd"] = tcpCmdToString(TcpCmd::ArtifactChunk);
    msg["task_id"] = taskId;
    msg["filename"] = filename;
    msg["chunk"] = chunkIndex;
    msg["total_chunks"] = totalChunks;
    msg["data"] = QString::fromLatin1(data.toBase64());
    return msg;
}

QJsonObject buildArtifactAck(int taskId, const QString &filename) {
    QJsonObject msg;
    msg["cmd"] = tcpCmdToString(TcpCmd::ArtifactAck);
    msg["task_id"] = taskId;
    msg["filename"] = filename;
    return msg;
}

QJsonObject buildError(const QString &message) {
    QJsonObject msg;
    msg["cmd"] = tcpCmdToString(TcpCmd::Error);
    msg["message"] = message;
    return msg;
}

QJsonObject buildPing() {
    QJsonObject msg;
    msg["cmd"] = tcpCmdToString(TcpCmd::Ping);
    return msg;
}

QJsonObject buildPong() {
    QJsonObject msg;
    msg["cmd"] = tcpCmdToString(TcpCmd::Pong);
    return msg;
}
