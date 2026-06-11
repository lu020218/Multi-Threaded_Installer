#pragma once

#include <string>
#include <vector>

namespace MultiThreadedInstaller {

// 卸载器自删除：uninstall.exe 无法删除正在运行的自己，需借助外部进程在本进程退出后清理。

/// 安排在本进程退出后删除自身（兜底方式，如 MoveFileEx 延迟到重启 / 启动清理助手）。
bool scheduleSelfDelete();
/// 立即启动一个分离的清理助手：等本进程退出后删除 cleanupRoots、manifest 及 uninstall.exe 自身。
bool scheduleSelfDeleteImmediate(const std::vector<std::string>& cleanupRoots,
                                 const std::string& manifestPath);

} // namespace MultiThreadedInstaller
