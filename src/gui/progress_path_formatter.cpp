#include "gui/progress_path_formatter.h"

#include <algorithm>
#include <cwchar>
#include <vector>

namespace MultiThreadedInstaller::GUIStatusPresenter {
namespace {

constexpr wchar_t kSeparator = L'\\';
constexpr const wchar_t* kMarker = L"...\\";
constexpr const wchar_t* kEllipsis = L"...";

bool StartsWith(const std::wstring& value, const std::wstring& prefix) {
    return value.size() >= prefix.size() &&
           std::equal(prefix.begin(), prefix.end(), value.begin());
}

std::wstring NormalizeForDisplay(std::wstring value) {
    if (StartsWith(value, L"\\\\?\\UNC\\")) {
        value = L"\\\\" + value.substr(8);
    } else if (StartsWith(value, L"\\\\?\\")) {
        value = value.substr(4);
    }

    std::replace(value.begin(), value.end(), L'/', kSeparator);
    return value;
}

std::wstring MiddleTruncate(const std::wstring& value, std::size_t maxChars) {
    if (value.size() <= maxChars) {
        return value;
    }
    if (maxChars <= 3) {
        return value.substr(0, maxChars);
    }

    const std::size_t remaining = maxChars - 3;
    const std::size_t head = remaining / 2;
    const std::size_t tail = remaining - head;
    return value.substr(0, head) + kEllipsis + value.substr(value.size() - tail);
}

std::size_t FindRootEnd(const std::wstring& path) {
    if (path.size() >= 3 && path[1] == L':' &&
        (path[2] == L'\\' || path[2] == L'/')) {
        return 3;
    }

    if (path.size() >= 2 && path[0] == L'\\' && path[1] == L'\\') {
        const std::size_t serverEnd = path.find(kSeparator, 2);
        if (serverEnd == std::wstring::npos) {
            return path.size();
        }
        const std::size_t shareEnd = path.find(kSeparator, serverEnd + 1);
        if (shareEnd == std::wstring::npos) {
            return path.size();
        }
        return shareEnd + 1;
    }

    return 0;
}

std::vector<std::wstring> SplitSegments(const std::wstring& value) {
    std::vector<std::wstring> segments;
    std::size_t start = 0;
    while (start < value.size()) {
        const std::size_t end = value.find(kSeparator, start);
        const std::size_t length =
            (end == std::wstring::npos) ? value.size() - start : end - start;
        if (length > 0) {
            segments.emplace_back(value.substr(start, length));
        }
        if (end == std::wstring::npos) {
            break;
        }
        start = end + 1;
    }
    return segments;
}

std::wstring JoinTailSegments(const std::vector<std::wstring>& segments,
                              std::size_t firstAllowedIndex,
                              const std::wstring& prefix,
                              std::size_t maxChars) {
    std::wstring suffix;
    for (std::size_t index = segments.size(); index-- > firstAllowedIndex;) {
        std::wstring candidate = segments[index];
        if (!suffix.empty()) {
            candidate += kSeparator;
            candidate += suffix;
        }

        if (prefix.size() + std::wcslen(kMarker) + candidate.size() > maxChars) {
            break;
        }
        suffix = std::move(candidate);
    }
    return suffix;
}

} // namespace

std::wstring FormatProgressPathForDisplay(const std::wstring& rawPath, std::size_t maxChars) {
    if (rawPath.empty() || maxChars == 0) {
        return {};
    }

    const std::wstring normalized = NormalizeForDisplay(rawPath);
    if (normalized.size() <= maxChars) {
        return normalized;
    }

    const std::size_t rootEnd = FindRootEnd(normalized);
    std::wstring prefix = normalized.substr(0, rootEnd);
    std::vector<std::wstring> segments = SplitSegments(normalized.substr(rootEnd));
    std::size_t firstTailIndex = 0;

    if (rootEnd == 0 && segments.size() > 1) {
        prefix = segments.front() + kSeparator;
        firstTailIndex = 1;
    }

    if (segments.empty() || prefix.size() + std::wcslen(kMarker) >= maxChars) {
        return MiddleTruncate(normalized, maxChars);
    }

    std::wstring suffix = JoinTailSegments(segments, firstTailIndex, prefix, maxChars);
    if (suffix.empty()) {
        return MiddleTruncate(normalized, maxChars);
    }

    return prefix + kMarker + suffix;
}

} // namespace MultiThreadedInstaller::GUIStatusPresenter
