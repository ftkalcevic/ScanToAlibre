// StepDialog.cpp : the workflow's modeless dialogs, one at a time.

#include "stdafx.h"
#include "ScanToAlibre.h"
#include "StepDialog.h"
#include "Log.h"
#include "ScanViewCommand.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

namespace
{
    CStepDialog* g_open = nullptr;
}

BEGIN_MESSAGE_MAP(CStepDialog, CDialog)
END_MESSAGE_MAP()

CStepDialog::CStepDialog(UINT templateId, const std::wstring& sessionId)
    : CDialog(templateId)
    , m_templateId(templateId)
    , m_sessionId(sessionId)
{
}

bool CStepDialog::Open()
{
    AFX_MANAGE_STATE(AfxGetStaticModuleState());

    CloseStepDialog();
    CWnd* pParent = (theApp.m_hAlibreWnd != nullptr) ? CWnd::FromHandle(theApp.m_hAlibreWnd) : nullptr;
    if (!Create(m_templateId, pParent))
    {
        Log(L"[dialog] couldn't make the dialog");
        delete this;
        return false;
    }
    g_open = this;
    ShowWindow(SW_SHOW);
    return true;
}

void CStepDialog::OnCancel()
{
    DestroyWindow();
}

void CStepDialog::OnOK()
{
    // Enter doesn't close a step dialog; each part has its own button.
}

void CStepDialog::PostNcDestroy()
{
    if (g_open == this)
    {
        g_open = nullptr;
        RefreshScanViews();     // the scan view stops drawing the scan (Show Scan's job)
    }
    CDialog::PostNcDestroy();
    delete this;
}

CStepDialog* OpenStepDialog()
{
    return g_open;
}

void CloseStepDialog()
{
    if (g_open != nullptr && ::IsWindow(g_open->GetSafeHwnd()))
        g_open->DestroyWindow();
    g_open = nullptr;
}
