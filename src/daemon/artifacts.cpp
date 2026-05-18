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

static const unsigned char DECODE_TABLE[256] = {
    ['A']=0,['B']=1,['C']=2,['D']=3,['E']=4,['F']=5,['G']=6,['H']=7,
    ['I']=8,['J']=9,['K']=10,['L']=11,['M']=12,['N']=13,['O']=14,['P']=15,
    ['Q']=16,['R']=17,['S']=18,['T']=19,['U']=20,['V']=21,['W']=22,['X']=23,
    ['Y']=24,['Z']=25,['a']=26,['b']=27,['c']=28,['d']=29,['e']=30,['f']=31,
    ['g']=32,['h']=33,['i']=34,['j']=35,['k']=36,['l']=37,['m']=38,['n']=39,
    ['o']=40,['p']=41,['q']=42,['r']=43,['s']=44,['t']=45,['u']=46,['v']=47,
    ['w']=48,['x']=49,['y']=50,['z']=51,['0']=52,['1']=53,['2']=54,['3']=55,
    ['4']=56,['5']=57,['6']=58,['7']=59,['8']=60,['9']=61,['+']=62,['/']=63,
};

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
        unsigned char a = DECODE_TABLE[(unsigned char)data[i]];
        unsigned char b = DECODE_TABLE[(unsigned char)data[i+1]];
        unsigned char c = DECODE_TABLE[(unsigned char)data[i+2]];
        unsigned char d = DECODE_TABLE[(unsigned char)data[i+3]];

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
