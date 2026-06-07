#include "common/version_utils.h"

#include <algorithm>
#include <cctype>
#include <string_view>
#include <vector>

namespace MultiThreadedInstaller {

namespace {

std::string TrimAsciiCopy(const std::string& value) {
    size_t start = 0;
    while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start])) != 0) {
        ++start;
    }
    size_t end = value.size();
    while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1])) != 0) {
        --end;
    }
    return value.substr(start, end - start);
}

std::vector<std::string> SplitVersion(const std::string& value) {
    std::vector<std::string> parts;
    size_t start = 0;
    while (start <= value.size()) {
        size_t end = value.find('.', start);
        if (end == std::string::npos) {
            end = value.size();
        }
        parts.push_back(value.substr(start, end - start));
        if (end == value.size()) {
            break;
        }
        start = end + 1;
    }
    return parts;
}

bool IsDigitsOnly(std::string_view value) {
    if (value.empty()) {
        return false;
    }
    return std::all_of(value.begin(), value.end(), [](unsigned char c) {
        return std::isdigit(c) != 0;
    });
}

uint64_t ParseNumericPart(std::string_view value) {
    uint64_t parsed = 0;
    for (unsigned char c : value) {
        parsed = parsed * 10ULL + static_cast<uint64_t>(c - '0');
    }
    return parsed;
}

int ComparePart(std::string_view lhs, std::string_view rhs) {
    const bool lhsDigits = IsDigitsOnly(lhs);
    const bool rhsDigits = IsDigitsOnly(rhs);
    if (lhsDigits && rhsDigits) {
        const uint64_t lhsValue = ParseNumericPart(lhs);
        const uint64_t rhsValue = ParseNumericPart(rhs);
        if (lhsValue < rhsValue) {
            return -1;
        }
        if (lhsValue > rhsValue) {
            return 1;
        }
        return 0;
    }

    std::string lhsLower(lhs);
    std::string rhsLower(rhs);
    std::transform(lhsLower.begin(), lhsLower.end(), lhsLower.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    std::transform(rhsLower.begin(), rhsLower.end(), rhsLower.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    if (lhsLower < rhsLower) {
        return -1;
    }
    if (lhsLower > rhsLower) {
        return 1;
    }
    return 0;
}

} // namespace

int compareSemanticVersion(const std::string& lhs, const std::string& rhs) {
    const std::string lhsTrimmed = TrimAsciiCopy(lhs);
    const std::string rhsTrimmed = TrimAsciiCopy(rhs);

    if (lhsTrimmed.empty() && rhsTrimmed.empty()) {
        return 0;
    }
    if (lhsTrimmed.empty()) {
        return -1;
    }
    if (rhsTrimmed.empty()) {
        return 1;
    }

    const std::vector<std::string> lhsParts = SplitVersion(lhsTrimmed);
    const std::vector<std::string> rhsParts = SplitVersion(rhsTrimmed);
    const size_t maxCount = (std::max)(lhsParts.size(), rhsParts.size());
    for (size_t i = 0; i < maxCount; ++i) {
        const std::string_view lhsPart = i < lhsParts.size() ? std::string_view(lhsParts[i]) : std::string_view("0");
        const std::string_view rhsPart = i < rhsParts.size() ? std::string_view(rhsParts[i]) : std::string_view("0");
        const int compare = ComparePart(lhsPart, rhsPart);
        if (compare != 0) {
            return compare;
        }
    }
    return 0;
}

std::string toNumericVersion(const std::string& version) {
    std::string core = TrimAsciiCopy(version);
    // 去掉 -prerelease 与 +buildmetadata 后缀。
    const size_t cut = core.find_first_of("-+");
    if (cut != std::string::npos) {
        core = core.substr(0, cut);
    }

    std::vector<std::string> parts = SplitVersion(core);
    std::string out;
    for (size_t i = 0; i < 4; ++i) {
        std::string part = i < parts.size() ? TrimAsciiCopy(parts[i]) : std::string();
        if (!IsDigitsOnly(part)) {
            part = "0";
        }
        if (i > 0) {
            out += '.';
        }
        out += part;
    }
    return out;
}

} // namespace MultiThreadedInstaller
