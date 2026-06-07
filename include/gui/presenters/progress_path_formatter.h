#pragma once

#include <cstddef>
#include <string>

namespace MultiThreadedInstaller::GUIStatusPresenter {

std::wstring FormatProgressPathForDisplay(const std::wstring& rawPath, std::size_t maxChars);

} // namespace MultiThreadedInstaller::GUIStatusPresenter
