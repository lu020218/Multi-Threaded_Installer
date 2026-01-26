#pragma once

#ifdef GUI_ENABLED

#include <Windows.h>
#include <string>
#include <cstdint>
#include "message_box_dialog.h"

namespace MultiThreadedInstaller {

/**
 * GUI辅助函数类
 * 提供文件浏览、磁盘空间查询、应用程序启动等辅助功能
 */
class GUIHelpers {
public:
    // 8.1 文件浏览对话框
    /**
     * 显示文件夹选择对话框
     * @param hParent 父窗口句柄
     * @param title 对话框标题
     * @param initialPath 初始路径（可选）
     * @param selectedPath 输出：用户选择的路径
     * @return 如果用户选择了路径返回true，取消返回false
     */
    static bool ShowFolderBrowserDialog(
        HWND hParent,
        const std::wstring& title,
        const std::wstring& initialPath,
        std::wstring& selectedPath);
    
    // 8.2 磁盘空间查询
    /**
     * 获取指定路径的可用磁盘空间
     * @param path 路径（可以是文件路径或目录路径）
     * @return 可用空间（字节），失败返回0
     */
    static uint64_t GetAvailableDiskSpace(const std::wstring& path);
    
    /**
     * 格式化字节数为可读字符串（如 "1.5 GB"）
     * @param bytes 字节数
     * @return 格式化后的字符串
     */
    static std::wstring FormatBytes(uint64_t bytes);
    
    /**
     * 检查磁盘空间是否充足
     * @param path 路径
     * @param requiredBytes 所需空间（字节）
     * @param availableBytes 输出：可用空间（字节）
     * @return 如果空间充足返回true
     */
    static bool CheckDiskSpace(
        const std::wstring& path,
        uint64_t requiredBytes,
        uint64_t& availableBytes);
    
    // 8.3 应用程序启动和网页打开
    /**
     * 启动已安装的应用程序
     * @param executablePath 可执行文件完整路径
     * @param workingDirectory 工作目录（可选，默认为可执行文件所在目录）
     * @return 如果启动成功返回true
     */
    static bool LaunchApplication(
        const std::wstring& executablePath,
        const std::wstring& workingDirectory = L"");
    
    /**
     * 在默认浏览器中打开网页
     * @param url 网页URL
     * @return 如果打开成功返回true
     */
    static bool OpenWebPage(const std::wstring& url);
    
    // 8.4 错误处理和对话框
    /**
     * 显示错误对话框
     * @param hParent 父窗口句柄
     * @param title 对话框标题
     * @param message 错误消息
     */
    static void ShowErrorDialog(
        HWND hParent,
        const std::wstring& title,
        const std::wstring& message);
    
    /**
     * 显示警告对话框
     * @param hParent 父窗口句柄
     * @param title 对话框标题
     * @param message 警告消息
     */
    static void ShowWarningDialog(
        HWND hParent,
        const std::wstring& title,
        const std::wstring& message);
    
    /**
     * 显示确认对话框
     * @param hParent 父窗口句柄
     * @param title 对话框标题
     * @param message 确认消息
     * @return 如果用户点击"是"返回true，点击"否"返回false
     */
    static bool ShowConfirmDialog(
        HWND hParent,
        const std::wstring& title,
        const std::wstring& message);
    
    /**
     * 显示信息对话框
     * @param hParent 父窗口句柄
     * @param title 对话框标题
     * @param message 信息消息
     */
    static void ShowInfoDialog(
        HWND hParent,
        const std::wstring& title,
        const std::wstring& message);

    /**
     * 鏄剧ず鑷畾涔夊璇濇
     * @param hParent 鐖剁獥鍙ｅ彞鏌?
     * @param title 瀵硅瘽妗嗘爣棰?
     * @param message 瀵硅瘽妗嗘枃鏈?
     * @param okText 确认按钮文本
     * @param cancelText 取消按钮文本（为空则隐藏）
     * @param altText 第三个按钮文本（为空则隐藏）
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
     * 获取当前UI语言代码
     */
    static std::wstring GetUILanguageCode();

    /**
     * 获取多语言文本
     * @param textId 文本ID
     * @param fallback 兜底文本
     */
    static std::wstring GetLocalizedText(const std::wstring& textId,
                                         const std::wstring& fallback);
    
    // 辅助函数
    /**
     * 初始化COM库（用于文件对话框）
     * @return 如果初始化成功返回true
     */
    static bool InitializeCOM();
    
    /**
     * 反初始化COM库
     */
    static void UninitializeCOM();
    
    /**
     * 验证路径格式是否有效
     * @param path 路径
     * @return 如果路径格式有效返回true
     */
    static bool ValidatePath(const std::wstring& path);
    
    /**
     * 提取驱动器根路径（如 "C:\\"）
     * @param path 完整路径
     * @return 驱动器根路径
     */
    static std::wstring ExtractRootPath(const std::wstring& path);
    
private:
    // 禁止实例化
    GUIHelpers() = delete;
    ~GUIHelpers() = delete;
    GUIHelpers(const GUIHelpers&) = delete;
    GUIHelpers& operator=(const GUIHelpers&) = delete;
};

} // namespace MultiThreadedInstaller

#endif // GUI_ENABLED
