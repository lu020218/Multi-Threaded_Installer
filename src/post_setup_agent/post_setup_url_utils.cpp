#include "post_setup_agent/post_setup_url_utils.h"

#include <algorithm>
#include <cctype>
#include <cstring>

namespace MultiThreadedInstaller {

namespace {

int HexValue(char ch) {
    if (ch >= '0' && ch <= '9') {
        return ch - '0';
    }
    if (ch >= 'a' && ch <= 'f') {
        return ch - 'a' + 10;
    }
    if (ch >= 'A' && ch <= 'F') {
        return ch - 'A' + 10;
    }
    return -1;
}

bool HasDrivePrefix(const std::string& value) {
    return value.size() >= 2 &&
           std::isalpha(static_cast<unsigned char>(value[0])) &&
           value[1] == ':';
}

} // namespace

std::string PercentDecodeUrlPath(const std::string& value) {
    std::string decoded;
    decoded.reserve(value.size());
    for (size_t i = 0; i < value.size(); ++i) {
        if (value[i] == '%' && i + 2 < value.size()) {
            const int hi = HexValue(value[i + 1]);
            const int lo = HexValue(value[i + 2]);
            if (hi >= 0 && lo >= 0) {
                decoded.push_back(static_cast<char>((hi << 4) | lo));
                i += 2;
                continue;
            }
        }
        decoded.push_back(value[i]);
    }
    return decoded;
}

std::string FileUrlToPath(const std::string& url) {
    constexpr const char* kPrefix = "file://";
    std::string path = url.size() >= std::strlen(kPrefix) ? url.substr(std::strlen(kPrefix)) : url;

    path = PercentDecodeUrlPath(path);

    if (!path.empty() && path[0] == '/' && path.size() > 2 &&
        std::isalpha(static_cast<unsigned char>(path[1])) && path[2] == ':') {
        path.erase(path.begin());
    }

    std::replace(path.begin(), path.end(), '/', '\\');

    if (!HasDrivePrefix(path) && path.rfind("\\\\", 0) != 0 &&
        path.rfind("\\", 0) != 0 && path.find('\\') != std::string::npos) {
        // file://server/share maps to a UNC path. Drive paths are handled above.
        path = "\\\\" + path;
    }

    return path;
}

} // namespace MultiThreadedInstaller
