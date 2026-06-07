#pragma once

#include "common/config_types.h"
#include "installer/state/install_state_store.h"
#include "installer/platform/path_resolver.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include <string>

namespace MultiThreadedInstaller {

HANDLE acquireInstallMutex(bool useMutex, const std::string& mutexName);
void releaseInstallMutex(HANDLE handle);

} // namespace MultiThreadedInstaller
