#pragma once

#include <UIlib.h>
#include <string>


namespace MultiThreadedInstaller {

/// 自定义消息框结果：最多三个按钮（确定/取消/备选）。
enum class DialogResult {
    Ok,      ///< 点击"确定"。
    Cancel,  ///< 点击"取消"或关闭。
    Alt      ///< 点击"备选"按钮。
};

/// 自绘消息框对话框（DuiLib 窗口）：标题 + 富文本消息 + 至多三个可定制按钮。
class MessageBoxDialog : public DuiLib::WindowImplBase {
public:
    /// @param okText/cancelText/altText 三个按钮文案，传空则不显示该按钮。
    MessageBoxDialog(const std::wstring& title,
                     const std::wstring& message,
                     const std::wstring& okText,
                     const std::wstring& cancelText,
                     const std::wstring& altText);

    /// 模态显示，返回用户选择。
    DialogResult ShowModal(HWND hParent);

protected:
    DuiLib::CDuiString GetSkinFile() override;     ///< 皮肤 XML 文件。
    LPCTSTR GetWindowClassName() const override;   ///< 窗口类名。
    void Notify(DuiLib::TNotifyUI& msg) override;  ///< 控件事件分发。
    void InitWindow() override;                    ///< 初始化（绑定控件、填文案）。

private:
    std::wstring m_title;        ///< 标题。
    std::wstring m_message;      ///< 消息正文。
    std::wstring m_okText;       ///< 确定按钮文案。
    std::wstring m_cancelText;   ///< 取消按钮文案。
    std::wstring m_altText;      ///< 备选按钮文案。
    DialogResult m_result;       ///< 选择结果。
    bool m_closeMeansOk;         ///< 关闭窗口是否等同"确定"。

    DuiLib::CLabelUI* m_pTitle;       ///< 标题控件。
    DuiLib::CRichEditUI* m_pMessage;  ///< 消息富文本控件。
    DuiLib::CButtonUI* m_pOk;         ///< 确定按钮。
    DuiLib::CButtonUI* m_pCancel;     ///< 取消按钮。
    DuiLib::CButtonUI* m_pAlt;        ///< 备选按钮。
};

} // namespace MultiThreadedInstaller

