#pragma once

#include <string>

namespace MultiThreadedInstaller {

/// 按语言代码从内嵌资源加载对应的许可协议文本（找不到时回退默认语言）。
std::wstring LoadLocalizedLicenseText(const std::wstring& languageCode);

} // namespace MultiThreadedInstaller
