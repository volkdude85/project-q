#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <cstring>

// ── UDP Heartbeat (24 bytes, fire-and-forget) ────────────────────
#pragma pack(push, 1)
struct HeartbeatPacket {
    uint32_t magic = 0x50514D00;  // "PQM\0"
    char nodeId[16] = {0};
    uint32_t load = 0;            // CPU load * 100
};
#pragma pack(pop)

// ── TCP Frame (length-prefixed JSON) ─────────────────────────────
struct TcpFrame {
    uint32_t length;   // network byte order
    std::string data;

    // Build from string
    static std::vector<char> encode(const std::string& json);

    // Parse from buffer (returns bytes consumed, or 0 if incomplete)
    static int decode(const char* buf, int buflen, TcpFrame& out);
};

// ── JSON message helpers ─────────────────────────────────────────
namespace Proto {
    // Register msg: {"cmd":"register","node":"porsche","capabilities":"gpu,x86_64"}
    std::string makeRegister(const std::string& node, const std::string& caps);

    // Task assign: {"cmd":"task_assign","task_id":N,"name":"...","cmd":"..."}
    std::string makeTaskAssign(int taskId, const std::string& name, const std::string& cmd);

    // Task result: {"cmd":"task_result","task_id":N,"status":"done|failed","duration_sec":N,"output":"..."}
    std::string makeTaskResult(int taskId, const std::string& status,
                               int durationSec, const std::string& output);

    // Node list response
    std::string makeNodeList(const std::string& jsonArray);

    // Heartbeat ACK
    std::string makeHeartbeatAck(uint32_t load);

    // Artifact sync request: {"cmd":"artifact_sync","task_id":N,"outputs":"file1,file2"}
    std::string makeArtifactSync(int taskId, const std::string& outputs);

    // Artifact data: {"cmd":"artifact_data","task_id":N,"path":"...","size":N,"data":"<base64>"}
    std::string makeArtifactData(int taskId, const std::string& path,
                                 int size, const std::string& base64Data);

    // Artifact ack: {"cmd":"artifact_ack","task_id":N,"path":"...","status":"ok|error"}
    std::string makeArtifactAck(int taskId, const std::string& path,
                                const std::string& status);

    // Parse cmd field from a JSON message
    std::string parseCmd(const std::string& json);
}
