#include "installer/platform/path_resolver.h"

#include "common/utf8_utils.h"

#include <windows.h>

namespace MultiThreadedInstaller {

std::string InstallerPathResolver::expandEnvironmentVariables(const std::string& path) {
    if (path.empty()) {
        return path;
    }

    std::wstring wide = Utf8ToWide(path);
    if (wide.empty()) {
        return path;
    }

    DWORD size = ExpandEnvironmentStringsW(wide.c_str(), nullptr, 0);
    if (size == 0) {
        return path;
    }

    std::wstring expanded;
    expanded.resize(size);
    DWORD written = ExpandEnvironmentStringsW(wide.c_str(), expanded.data(), size);
    if (written == 0) {
        return path;
    }
    if (!expanded.empty() && expanded.back() == L'\0') {
        expanded.pop_back();
    }
    return WideToUtf8(expanded);
}

} // namespace MultiThreadedInstaller
