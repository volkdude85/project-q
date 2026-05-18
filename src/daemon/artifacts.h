#pragma once
#include <string>
#include <vector>

namespace ArtifactSync {

// Encode a file to base64 string
std::string fileToBase64(const std::string& path);

// Decode and write base64 data to file
bool base64ToFile(const std::string& data, const std::string& path);

// Scan artifact dir for output files matching a comma-separated list
std::vector<std::string> findOutputs(const std::string& artifactDir,
                                      const std::string& outputsCsv);

}
