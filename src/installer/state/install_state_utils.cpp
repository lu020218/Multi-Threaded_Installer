#include "installer/state/install_state_utils.h"

#include "common/utf8_utils.h"

#ifdef _WIN32
#include <windows.h>
#endif

namespace MultiThreadedInstaller {

HANDLE acquireInstallMutex(bool useMutex, const std::string& mutexName) {
#ifdef _WIN32
    if (!useMutex || mutexName.empty()) {
        return nullptr;
    }
    std::wstring name = Utf8ToWide(mutexName);
    if (name.empty()) {
        return nullptr;
    }
    return CreateMutexW(nullptr, FALSE, name.c_str());
#else
    (void)useMutex;
    (void)mutexName;
    return nullptr;
#endif
}

void releaseInstallMutex(HANDLE handle) {
#ifdef _WIN32
    if (handle) {
        CloseHandle(handle);
    }
#else
    (void)handle;
#endif
}

} // namespace MultiThreadedInstaller
