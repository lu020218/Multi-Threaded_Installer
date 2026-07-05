#pragma once

#include <functional>
#include <string>

namespace MultiThreadedInstaller {

// 带重试的 PE 资源更新会话。
//
// applyFn 收到一个打开的资源更新句柄（void* 实为 HANDLE），在其上做若干 UpdateResource，
// 返回 true 表示已应用（尚未提交）。本函数负责「打开(BeginUpdateResource)→应用→
// 提交(EndUpdateResource)」整个会话，并对打开/提交阶段的「分享/锁/拒绝访问」错误
// （ERROR_SHARING_VIOLATION / ERROR_LOCK_VIOLATION / ERROR_ACCESS_DENIED）退避重试整轮会话，
// 解决打包时杀软(Defender 实时保护)间歇性锁住临时模板 exe 导致的资源写入失败。
//
// applyFn 自身的失败视为真实错误（非文件锁），不重试，直接丢弃会话并返回 false。
bool RunResourceUpdateSession(
    const std::wstring& exePath,
    const std::function<bool(void* update, std::string& error)>& applyFn,
    std::string& error);

} // namespace MultiThreadedInstaller
