#include "artifacts.h"
#include <fstream>
#include <sstream>
#include <vector>
#include <cstring>
#include <sys/stat.h>

namespace {

const char BASE64_CHARS[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "abcdefghijklmnopqrstuvwxyz"
    "0123456789+/";

std::string encodeBlock(const unsigned char* in, int len) {
    std::string out;
    out += BASE64_CHARS[in[0] >> 2];
    out += BASE64_CHARS[((in[0] & 0x03) << 4) | (len > 1 ? (in[1] >> 4) : 0)];
    if (len > 1) {
        out += BASE64_CHARS[((in[1] & 0x0F) << 2) | (len > 2 ? (in[2] >> 6) : 0)];
    } else {
        out += '=';
    }
    if (len > 2) {
        out += BASE64_CHARS[in[2] & 0x3F];
    } else {
        out += '=';
    }
    return out;
}

static unsigned char decodeBase64Char(unsigned char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return 0;
}

}

std::string ArtifactSync::fileToBase64(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return "";

    std::vector<unsigned char> buf((std::istreambuf_iterator<char>(file)),
                                    std::istreambuf_iterator<char>());
    file.close();

    std::string result;
    result.reserve(((buf.size() + 2) / 3) * 4);

    for (size_t i = 0; i < buf.size(); i += 3) {
        int remaining = buf.size() - i;
        result += encodeBlock(&buf[i], remaining > 3 ? 3 : remaining);
    }

    return result;
}

bool ArtifactSync::base64ToFile(const std::string& data, const std::string& path) {
    std::vector<unsigned char> out;
    out.reserve((data.size() / 4) * 3);

    for (size_t i = 0; i < data.size(); i += 4) {
        if (data[i] == '=' || i + 3 >= data.size()) break;
        unsigned char a = decodeBase64Char((unsigned char)data[i]);
        unsigned char b = decodeBase64Char((unsigned char)data[i+1]);
        unsigned char c = decodeBase64Char((unsigned char)data[i+2]);
        unsigned char d = decodeBase64Char((unsigned char)data[i+3]);

        out.push_back((a << 2) | (b >> 4));
        if (data[i+2] != '=') out.push_back(((b & 0x0F) << 4) | (c >> 2));
        if (data[i+3] != '=') out.push_back(((c & 0x03) << 6) | d);
    }

    std::ofstream file(path, std::ios::binary);
    if (!file) return false;
    file.write(reinterpret_cast<const char*>(out.data()), out.size());
    file.close();
    return true;
}

std::vector<std::string> ArtifactSync::findOutputs(const std::string& artifactDir,
                                                    const std::string& outputsCsv) {
    std::vector<std::string> found;
    if (outputsCsv.empty()) return found;

    std::stringstream ss(outputsCsv);
    std::string item;
    while (std::getline(ss, item, ',')) {
        // Trim whitespace
        auto start = item.find_first_not_of(" \t");
        auto end = item.find_last_not_of(" \t");
        if (start == std::string::npos) continue;
        item = item.substr(start, end - start + 1);

        // Check full path first, then artifact dir
        struct stat st;
        if (stat(item.c_str(), &st) == 0) {
            found.push_back(item);
        } else {
            std::string inDir = artifactDir + "/" + item;
            if (stat(inDir.c_str(), &st) == 0) {
                found.push_back(inDir);
            }
        }
    }
    return found;
}
