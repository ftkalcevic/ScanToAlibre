// StepDialog.h : the workflow's modeless dialogs (Refine, later Orient).
//
// Only one is open at a time (Frank's rule): opening one closes any other.
// Each works on one part session's scan and applies its changes straight away,
// so closing one loses nothing.

#pragma once

#include <string>

class CStepDialog : public CDialog
{
public:
    CStepDialog(UINT templateId, const std::wstring& sessionId);

    // Makes and shows the dialog, closing whichever step dialog was open.
    // Returns false if it couldn't be made. The dialog deletes itself when closed.
    bool Open();

    const std::wstring& SessionId() const { return m_sessionId; }

protected:
    void OnCancel() override;
    void OnOK() override;
    void PostNcDestroy() override;

    UINT m_templateId;
    std::wstring m_sessionId;

    DECLARE_MESSAGE_MAP()
};

// The open step dialog, or nullptr.
CStepDialog* OpenStepDialog();

// Closes it, if one is open (at unload).
void CloseStepDialog();
