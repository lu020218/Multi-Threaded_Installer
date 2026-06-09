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

/// 获取全局安装互斥量，避免同一产品并发安装/卸载。
/// @param useMutex false 时直接返回 nullptr（不加锁）。
/// @param mutexName 互斥量名（通常 EngineDefaults::MutexName(产品名)）。
/// @return 互斥量句柄（由调用方长期持有，安装结束用 releaseInstallMutex 释放）；失败返回 nullptr。
/// @note 这是刻意的所有权转移（句柄生命周期超出本函数），故返回裸 HANDLE 而非 RAII 包装。
HANDLE acquireInstallMutex(bool useMutex, const std::string& mutexName);
/// 释放 acquireInstallMutex 返回的互斥量句柄（nullptr 安全）。
void releaseInstallMutex(HANDLE handle);

} // namespace MultiThreadedInstaller
