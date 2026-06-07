#include "installer/hooks/component_launcher.h"

#include "common/utf8_utils.h"

#include <algorithm>
#include <cwctype>

namespace MultiThreadedInstaller {
namespace {

std::wstring QuoteProcessPath(const std::wstring& value) {
    if (value.empty()) {
        return L"\"\"";
    }
    if (value.front() == L'"' && value.back() == L'"') {
        return value;
    }
    return L"\"" + value + L"\"";
}

std::wstring LowerExtension(const std::filesystem::path& executablePath) {
    std::wstring extension = executablePath.extension().wstring();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](wchar_t ch) { return static_cast<wchar_t>(std::towlower(ch)); });
    return extension;
}

} // namespace

ComponentLaunchCommand BuildComponentLaunchCommand(const std::filesystem::path& executablePath,
                                                   const std::string& args) {
    ComponentLaunchCommand command;
    const std::wstring executableW = executablePath.wstring();
    const std::wstring argsW = Utf8ToWide(args);
    const std::wstring extension = LowerExtension(executablePath);

    if (extension == L".bat" || extension == L".cmd") {
        command.type = ComponentLauncherType::Batch;
        command.commandLine = L"cmd.exe /c " + QuoteProcessPath(executableW);
        command.hideByDefault = true;
        command.startFailureMessage = "Failed to start batch component installer.";
    } else if (extension == L".ps1") {
        command.type = ComponentLauncherType::PowerShell;
        command.commandLine =
            L"powershell.exe -NoProfile -ExecutionPolicy Bypass -File " +
            QuoteProcessPath(executableW);
        command.startFailureMessage = "Failed to start PowerShell component installer.";
    } else if (extension == L".msi") {
        command.type = ComponentLauncherType::Msi;
        command.commandLine = L"msiexec.exe /i " + QuoteProcessPath(executableW);
        command.startFailureMessage = "Failed to start MSI component installer.";
    } else {
        command.type = ComponentLauncherType::Direct;
        command.commandLine = QuoteProcessPath(executableW);
        command.startFailureMessage = "Failed to start component process.";
    }

    if (!argsW.empty()) {
        command.commandLine.append(L" ");
        command.commandLine.append(argsW);
    }
    return command;
}

} // namespace MultiThreadedInstaller
