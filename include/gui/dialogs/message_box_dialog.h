#pragma once

#include <UIlib.h>
#include <string>


namespace MultiThreadedInstaller {

enum class DialogResult {
    Ok,
    Cancel,
    Alt
};

class MessageBoxDialog : public DuiLib::WindowImplBase {
public:
    MessageBoxDialog(const std::wstring& title,
                     const std::wstring& message,
                     const std::wstring& okText,
                     const std::wstring& cancelText,
                     const std::wstring& altText);

    DialogResult ShowModal(HWND hParent);

protected:
    DuiLib::CDuiString GetSkinFile() override;
    LPCTSTR GetWindowClassName() const override;
    void Notify(DuiLib::TNotifyUI& msg) override;
    void InitWindow() override;

private:
    std::wstring m_title;
    std::wstring m_message;
    std::wstring m_okText;
    std::wstring m_cancelText;
    std::wstring m_altText;
    DialogResult m_result;
    bool m_closeMeansOk;

    DuiLib::CLabelUI* m_pTitle;
    DuiLib::CRichEditUI* m_pMessage;
    DuiLib::CButtonUI* m_pOk;
    DuiLib::CButtonUI* m_pCancel;
    DuiLib::CButtonUI* m_pAlt;
};

} // namespace MultiThreadedInstaller

