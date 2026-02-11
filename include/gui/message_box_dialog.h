#pragma once

#ifdef GUI_ENABLED

#include <UIlib.h>
#include <string>

using namespace DuiLib;

namespace MultiThreadedInstaller {

enum class DialogResult {
    Ok,
    Cancel,
    Alt
};

class MessageBoxDialog : public WindowImplBase {
public:
    MessageBoxDialog(const std::wstring& title,
                     const std::wstring& message,
                     const std::wstring& okText,
                     const std::wstring& cancelText,
                     const std::wstring& altText);

    DialogResult ShowModal(HWND hParent);

protected:
    CDuiString GetSkinFile() override;
    LPCTSTR GetWindowClassName() const override;
    void Notify(TNotifyUI& msg) override;
    void InitWindow() override;

private:
    std::wstring m_title;
    std::wstring m_message;
    std::wstring m_okText;
    std::wstring m_cancelText;
    std::wstring m_altText;
    DialogResult m_result;
    bool m_closeMeansOk;

    CLabelUI* m_pTitle;
    CRichEditUI* m_pMessage;
    CButtonUI* m_pOk;
    CButtonUI* m_pCancel;
    CButtonUI* m_pAlt;
};

} // namespace MultiThreadedInstaller

#endif // GUI_ENABLED
