#pragma once

#include <Windows.h>
#include <string>
#include <cstdint>
#include "message_box_dialog.h"

namespace MultiThreadedInstaller {

/**
 *
 *
 */
class GUIHelpers {
public:

    /**
     *
     *
     *
     *
     *
     *
     */
    static bool ShowFolderBrowserDialog(
        HWND hParent,
        const std::wstring& title,
        const std::wstring& initialPath,
        std::wstring& selectedPath);
    

    /**
     *
     *
     *
     */
    static uint64_t GetAvailableDiskSpace(const std::wstring& path);

    /**
     *
     *
     *
     */
    static uint64_t GetTotalDiskSpace(const std::wstring& path);
    
    /**
     *
     *
     *
     */
    static std::wstring FormatBytes(uint64_t bytes);
    
    /**
     *
     *
     *
     *
     *
     */
    static bool CheckDiskSpace(
        const std::wstring& path,
        uint64_t requiredBytes,
        uint64_t& availableBytes);
    

    /**
     *
     *
     *
     *
     */
    static bool LaunchApplication(
        const std::wstring& executablePath,
        const std::wstring& workingDirectory = L"");
    
    /**
     *
     *
     *
     */
    static bool OpenWebPage(const std::wstring& url);
    

    /**
     *
     *
     *
     *
     */
    static void ShowErrorDialog(
        HWND hParent,
        const std::wstring& title,
        const std::wstring& message);
    
    /**
     *
     *
     *
     *
     */
    static void ShowWarningDialog(
        HWND hParent,
        const std::wstring& title,
        const std::wstring& message);
    
    /**
     *
     *
     *
     *
     *
     */
    static bool ShowConfirmDialog(
        HWND hParent,
        const std::wstring& title,
        const std::wstring& message);
    
    /**
     *
     *
     *
     *
     */
    static void ShowInfoDialog(
        HWND hParent,
        const std::wstring& title,
        const std::wstring& message);

    /**
     *
     *
     *
     *
     *
     *
     *
     * @return DialogResult
     */
    static DialogResult ShowCustomDialog(
        HWND hParent,
        const std::wstring& title,
        const std::wstring& message,
        const std::wstring& okText,
        const std::wstring& cancelText = L"",
        const std::wstring& altText = L"");

    /**
     *
     */
    static std::wstring GetUILanguageCode();

    /**
     *
     *
     *
     */
    static std::wstring GetLocalizedText(const std::wstring& textId,
                                         const std::wstring& fallback);
    

    /**
     *
     *
     */
    static bool InitializeCOM();
    
    /**
     *
     */
    static void UninitializeCOM();
    
    /**
     *
     *
     *
     */
    static bool ValidatePath(const std::wstring& path);
    
    /**
     *
     *
     *
     */
    static std::wstring ExtractRootPath(const std::wstring& path);
    
private:

    GUIHelpers() = delete;
    ~GUIHelpers() = delete;
    GUIHelpers(const GUIHelpers&) = delete;
    GUIHelpers& operator=(const GUIHelpers&) = delete;
};

} // namespace MultiThreadedInstaller

