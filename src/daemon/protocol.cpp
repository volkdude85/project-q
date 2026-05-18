#include "protocol.h"
#include <cstring>
#include <sstream>
#include <arpa/inet.h>

// ── TCP Frame encode ─────────────────────────────────────────────
std::vector<char> TcpFrame::encode(const std::string& json) {
    uint32_t len = htonl(static_cast<uint32_t>(json.size()));
    std::vector<char> buf(sizeof(len) + json.size());
    memcpy(buf.data(), &len, sizeof(len));
    memcpy(buf.data() + sizeof(len), json.data(), json.size());
    return buf;
}

// ── TCP Frame decode ─────────────────────────────────────────────
int TcpFrame::decode(const char* buf, int buflen, TcpFrame& out) {
    if (buflen < 4) return 0;  // need length prefix
    uint32_t netLen;
    memcpy(&netLen, buf, 4);
    uint32_t msgLen = ntohl(netLen);
    if (static_cast<uint32_t>(buflen - 4) < msgLen) return 0; // incomplete
    out.length = msgLen;
    out.data.assign(buf + 4, msgLen);
    return 4 + msgLen;
}

// ── JSON helpers (minimal — no json lib dep) ─────────────────────
// These produce valid JSON manually. For complex parsing we'd
// link jsoncpp or nlohmann, but for now manual is fine.

static std::string jsonEscape(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (c == '"') out += "\\\"";
        else if (c == '\\') out += "\\\\";
        else if (c == '\n') out += "\\n";
        else if (c == '\r') out += "\\r";
        else if (c == '\t') out += "\\t";
        else out += c;
    }
    return out;
}

std::string Proto::makeRegister(const std::string& node, const std::string& caps) {
    return "{\"cmd\":\"register\",\"node\":\"" + jsonEscape(node) +
           "\",\"capabilities\":\"" + jsonEscape(caps) + "\"}";
}

std::string Proto::makeTaskAssign(int taskId, const std::string& name, const std::string& cmd) {
    return "{\"cmd\":\"task_assign\",\"task_id\":" + std::to_string(taskId) +
           ",\"name\":\"" + jsonEscape(name) +
           "\",\"cmd\":\"" + jsonEscape(cmd) + "\"}";
}

std::string Proto::makeTaskResult(int taskId, const std::string& status,
                                   int durationSec, const std::string& output) {
    return "{\"cmd\":\"task_result\",\"task_id\":" + std::to_string(taskId) +
           ",\"status\":\"" + status +
           "\",\"duration_sec\":" + std::to_string(durationSec) +
           ",\"output\":\"" + jsonEscape(output) + "\"}";
}

std::string Proto::makeNodeList(const std::string& jsonArray) {
    return "{\"cmd\":\"node_list\",\"nodes\":" + jsonArray + "}";
}

std::string Proto::makeHeartbeatAck(uint32_t load) {
    return "{\"cmd\":\"heartbeat_ack\",\"load\":" + std::to_string(load) + "}";
}

std::string Proto::makeArtifactSync(int taskId, const std::string& outputs) {
    return "{\"cmd\":\"artifact_sync\",\"task_id\":" + std::to_string(taskId) +
           ",\"outputs\":\"" + jsonEscape(outputs) + "\"}";
}

std::string Proto::makeArtifactData(int taskId, const std::string& path,
                                     int size, const std::string& base64Data) {
    return "{\"cmd\":\"artifact_data\",\"task_id\":" + std::to_string(taskId) +
           ",\"path\":\"" + jsonEscape(path) +
           "\",\"size\":" + std::to_string(size) +
           ",\"data\":\"" + base64Data + "\"}";
}

std::string Proto::makeArtifactAck(int taskId, const std::string& path,
                                    const std::string& status) {
    return "{\"cmd\":\"artifact_ack\",\"task_id\":" + std::to_string(taskId) +
           ",\"path\":\"" + jsonEscape(path) +
           "\",\"status\":\"" + status + "\"}";
}

// Parse first JSON key after "cmd": — naive, no json parser
std::string Proto::parseCmd(const std::string& json) {
    // Find "cmd":"..." in the JSON
    auto cmdPos = json.find("\"cmd\"");
    if (cmdPos == std::string::npos) return "";
    auto colon = json.find(':', cmdPos + 5);
    if (colon == std::string::npos) return "";
    auto firstQuote = json.find('"', colon + 1);
    if (firstQuote == std::string::npos) return "";
    auto secondQuote = json.find('"', firstQuote + 1);
    if (secondQuote == std::string::npos) return "";
    return json.substr(firstQuote + 1, secondQuote - firstQuote - 1);
}
