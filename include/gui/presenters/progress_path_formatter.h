#pragma once

#include <cstddef>
#include <string>

namespace MultiThreadedInstaller::GUIStatusPresenter {

/// 将进度中的长路径格式化为适合显示的短形式（超过 maxChars 时中间省略），避免撑爆 UI。
std::wstring FormatProgressPathForDisplay(const std::wstring& rawPath, std::size_t maxChars);

} // namespace MultiThreadedInstaller::GUIStatusPresenter
