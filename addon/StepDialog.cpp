// StepDialog.cpp : the workflow's modeless dialogs, one at a time.

#include "stdafx.h"
#include "ScanToAlibre.h"
#include "StepDialog.h"
#include "Help.h"
#include "Log.h"
#include "ScanViewCommand.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

namespace
{
    CStepDialog* g_open = nullptr;

    // The help page's section for each dialog, by its template.
    const wchar_t* HelpSection(UINT templateId)
    {
        switch (templateId)
        {
        case IDD_REFINE:         return L"refine";
        case IDD_ORIENT:         return L"orient";
        case IDD_SECTION:        return L"section";
        case IDD_SECTION_SERIES: return L"section-series";
        case IDD_SURFACE:        return L"surface";
        case IDD_SILHOUETTE:     return L"silhouette";
        case IDD_COMPARE:        return L"compare";
        default:                 return nullptr;
        }
    }
}

BEGIN_MESSAGE_MAP(CStepDialog, CDialog)
    ON_BN_CLICKED(IDC_HELP_BUTTON, &CStepDialog::OnHelpButton)
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

void CStepDialog::OnHelpButton()
{
    ShowHelp(HelpSection(m_templateId));
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
