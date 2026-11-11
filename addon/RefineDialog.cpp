// RefineDialog.cpp : Refine: decimate and smooth the part's scan.

#include "stdafx.h"
#include "ScanToAlibre.h"
#include "RefineDialog.h"
#include "ScanDocument.h"
#include "ScanImport.h"
#include "ScanViewCommand.h"
#include "Log.h"

#include "Decimate.h"
#include "Smooth.h"

#include <chrono>
#include <cwchar>

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

namespace
{
    // What the dialog shows when it opens again, for this Alibre session.
    struct RefineChoices
    {
        bool byPercent = true;
        double count = 100000;
        double percent = 50;
        bool limit = false;
        double limitMm = 0.05;
        bool decimateKeepEdges = true;
        int method = 2;             // Bilateral
        double passes = 5;
        double strength = 0.5;
        bool smoothKeepEdges = true;
    };
    RefineChoices g_choices;

    const wchar_t* const kMethods[] = {
        L"Laplacian (simple; shrinks the shape)",
        L"Taubin (keeps the size)",
        L"Bilateral (keeps sharp edges)",
    };

    double Seconds(std::chrono::steady_clock::time_point since)
    {
        return std::chrono::duration<double>(std::chrono::steady_clock::now() - since).count();
    }

    CString Number(double value)
    {
        CString text;
        text.Format(L"%g", value);
        return text;
    }

    CString Thousands(size_t value)
    {
        CString digits;
        digits.Format(L"%zu", value);
        CString out;
        const int length = digits.GetLength();
        for (int i = 0; i < length; ++i)
        {
            out += digits[i];
            if ((length - i - 1) % 3 == 0 && i != length - 1)
                out += L',';
        }
        return out;
    }
}

BEGIN_MESSAGE_MAP(CRefineDlg, CStepDialog)
    ON_BN_CLICKED(IDC_DEC_APPLY, &CRefineDlg::OnDecimate)
    ON_BN_CLICKED(IDC_SMOOTH_APPLY, &CRefineDlg::OnSmooth)
    ON_BN_CLICKED(IDC_REFINE_UNDO, &CRefineDlg::OnUndo)
    ON_BN_CLICKED(IDC_DEC_BY_COUNT, &CRefineDlg::OnDecimateKind)
    ON_BN_CLICKED(IDC_DEC_BY_PERCENT, &CRefineDlg::OnDecimateKind)
    ON_BN_CLICKED(IDC_DEC_LIMIT, &CRefineDlg::OnLimitChanged)
END_MESSAGE_MAP()

CRefineDlg::CRefineDlg(const std::wstring& sessionId)
    : CStepDialog(IDD_REFINE, sessionId)
{
}

BOOL CRefineDlg::OnInitDialog()
{
    CStepDialog::OnInitDialog();

    m_method.SubclassDlgItem(IDC_SMOOTH_METHOD, this);
    for (const wchar_t* method : kMethods)
        m_method.AddString(method);

    const RefineChoices& c = g_choices;
    CheckRadioButton(IDC_DEC_BY_COUNT, IDC_DEC_BY_PERCENT, c.byPercent ? IDC_DEC_BY_PERCENT : IDC_DEC_BY_COUNT);
    SetDlgItemText(IDC_DEC_COUNT, Number(c.count));
    SetDlgItemText(IDC_DEC_PERCENT, Number(c.percent));
    CheckDlgButton(IDC_DEC_LIMIT, c.limit ? BST_CHECKED : BST_UNCHECKED);
    SetDlgItemText(IDC_DEC_LIMIT_MM, Number(c.limitMm));
    CheckDlgButton(IDC_DEC_KEEP_EDGES, c.decimateKeepEdges ? BST_CHECKED : BST_UNCHECKED);
    m_method.SetCurSel(c.method);
    SetDlgItemText(IDC_SMOOTH_PASSES, Number(c.passes));
    SetDlgItemText(IDC_SMOOTH_STRENGTH, Number(c.strength));
    CheckDlgButton(IDC_SMOOTH_KEEP_EDGES, c.smoothKeepEdges ? BST_CHECKED : BST_UNCHECKED);

    OnDecimateKind();
    OnLimitChanged();
    ShowScanInfo();
    SetStatus(L"");
    return TRUE;
}

void CRefineDlg::OnDecimateKind()
{
    const bool byPercent = IsDlgButtonChecked(IDC_DEC_BY_PERCENT) == BST_CHECKED;
    GetDlgItem(IDC_DEC_COUNT)->EnableWindow(!byPercent);
    GetDlgItem(IDC_DEC_PERCENT)->EnableWindow(byPercent);
}

void CRefineDlg::OnLimitChanged()
{
    GetDlgItem(IDC_DEC_LIMIT_MM)->EnableWindow(IsDlgButtonChecked(IDC_DEC_LIMIT) == BST_CHECKED);
}

void CRefineDlg::ShowScanInfo()
{
    const ScanDocument* pScan = FindScan(m_sessionId);
    CString text;
    if (pScan == nullptr)
    {
        text = L"This part has no scan. Load one first.";
    }
    else
    {
        const scancore::Point3 size = pScan->bounds.Size();
        text.Format(L"%s: %s triangles, %.1f x %.1f x %.1f mm", ScanDisplayName(pScan->sourceName).c_str(),
                    static_cast<LPCWSTR>(Thousands(pScan->mesh.triangles.size())), size.x, size.y, size.z);
    }
    SetDlgItemText(IDC_REFINE_INFO, text);
    GetDlgItem(IDC_DEC_APPLY)->EnableWindow(pScan != nullptr);
    GetDlgItem(IDC_SMOOTH_APPLY)->EnableWindow(pScan != nullptr);
    GetDlgItem(IDC_REFINE_UNDO)->EnableWindow(pScan != nullptr && pScan->CanUndo());
}

void CRefineDlg::SetStatus(const CString& text)
{
    SetDlgItemText(IDC_REFINE_STATUS, text);
    GetDlgItem(IDC_REFINE_STATUS)->UpdateWindow();
}

bool CRefineDlg::ReadNumber(int id, double& value)
{
    CString text;
    GetDlgItemText(id, text);
    text.Trim();
    wchar_t* end = nullptr;
    const double read = wcstod(text, &end);
    if (text.IsEmpty() || end == nullptr || *end != L'\0' || !std::isfinite(read))
    {
        GotoDlgCtrl(GetDlgItem(id));
        SetStatus(L"Please enter a number there.");
        MessageBeep(MB_ICONWARNING);
        return false;
    }
    value = read;
    return true;
}

void CRefineDlg::ScanChanged()
{
    MarkPartChanged(m_sessionId, L"Scan refined. Changed by ScanToAlibre so the part saves its scan.");
    RefreshScanViews();
    ShowScanInfo();
}

void CRefineDlg::OnDecimate()
{
    AFX_MANAGE_STATE(AfxGetStaticModuleState());

    ScanDocument* pScan = FindScan(m_sessionId);
    if (pScan == nullptr)
        return;

    RefineChoices& c = g_choices;
    c.byPercent = IsDlgButtonChecked(IDC_DEC_BY_PERCENT) == BST_CHECKED;
    c.limit = IsDlgButtonChecked(IDC_DEC_LIMIT) == BST_CHECKED;
    c.decimateKeepEdges = IsDlgButtonChecked(IDC_DEC_KEEP_EDGES) == BST_CHECKED;

    const size_t current = pScan->mesh.triangles.size();
    scancore::DecimateOptions options;
    if (c.byPercent)
    {
        if (!ReadNumber(IDC_DEC_PERCENT, c.percent))
            return;
        if (c.percent <= 0.0 || c.percent >= 100.0)
        {
            SetStatus(L"The percent must be more than 0 and less than 100.");
            MessageBeep(MB_ICONWARNING);
            return;
        }
        options.targetTriangles = static_cast<size_t>(current * c.percent / 100.0);
    }
    else
    {
        if (!ReadNumber(IDC_DEC_COUNT, c.count))
            return;
        if (c.count < 4.0 || c.count >= static_cast<double>(current))
        {
            SetStatus(L"The number of triangles must be at least 4 and less than the scan has now.");
            MessageBeep(MB_ICONWARNING);
            return;
        }
        options.targetTriangles = static_cast<size_t>(c.count);
    }
    if (c.limit)
    {
        if (!ReadNumber(IDC_DEC_LIMIT_MM, c.limitMm))
            return;
        if (c.limitMm <= 0.0)
        {
            SetStatus(L"The largest change must be more than 0 mm.");
            MessageBeep(MB_ICONWARNING);
            return;
        }
        options.maxError = c.limitMm;
    }
    options.keepOpenEdges = c.decimateKeepEdges;

    SetStatus(L"Decimating...");
    CWaitCursor wait;
    const auto start = std::chrono::steady_clock::now();
    scancore::DecimateResult result;
    scancore::Mesh decimated = scancore::Decimate(pScan->mesh, options, &result);
    const double seconds = Seconds(start);
    Log(L"[refine] decimate %zu to %zu (target %zu, limit %g mm, keep edges %d): %zu collapses, %zu refused, "
        L"largest change about %.4f mm%s, %.1f s",
        result.trianglesBefore, result.trianglesAfter, options.targetTriangles, options.maxError,
        options.keepOpenEdges ? 1 : 0, result.collapses, result.refused, result.largestError,
        result.stoppedByError ? L", stopped by the limit" : L"", seconds);

    if (result.trianglesAfter >= result.trianglesBefore)
    {
        SetStatus(result.stoppedByError ? L"Nothing removed: every change would be larger than the limit."
                                        : L"Nothing removed.");
        return;
    }
    pScan->ReplaceMesh(std::move(decimated));
    ScanChanged();

    CString status;
    status.Format(L"Decimated from %s to %s triangles in %.1f s; largest change about %.3f mm%s.",
                  static_cast<LPCWSTR>(Thousands(result.trianglesBefore)),
                  static_cast<LPCWSTR>(Thousands(result.trianglesAfter)), seconds, result.largestError,
                  result.stoppedByError ? L", stopped by the limit" : L"");
    SetStatus(status);
}

void CRefineDlg::OnSmooth()
{
    AFX_MANAGE_STATE(AfxGetStaticModuleState());

    ScanDocument* pScan = FindScan(m_sessionId);
    if (pScan == nullptr)
        return;

    RefineChoices& c = g_choices;
    c.method = m_method.GetCurSel();
    c.smoothKeepEdges = IsDlgButtonChecked(IDC_SMOOTH_KEEP_EDGES) == BST_CHECKED;
    if (!ReadNumber(IDC_SMOOTH_PASSES, c.passes) || !ReadNumber(IDC_SMOOTH_STRENGTH, c.strength))
        return;
    if (c.passes < 1.0 || c.passes > 100.0 || c.passes != static_cast<int>(c.passes))
    {
        SetStatus(L"Passes must be a whole number from 1 to 100.");
        MessageBeep(MB_ICONWARNING);
        return;
    }
    if (c.strength <= 0.0 || c.strength > 1.0)
    {
        SetStatus(L"Strength must be more than 0 and at most 1.");
        MessageBeep(MB_ICONWARNING);
        return;
    }

    scancore::SmoothOptions options;
    options.method = (c.method == 0)   ? scancore::SmoothMethod::Laplacian
                     : (c.method == 1) ? scancore::SmoothMethod::Taubin
                                       : scancore::SmoothMethod::Bilateral;
    options.passes = static_cast<int>(c.passes);
    options.strength = c.strength;
    options.keepOpenEdges = c.smoothKeepEdges;

    SetStatus(L"Smoothing...");
    CWaitCursor wait;
    const auto start = std::chrono::steady_clock::now();
    scancore::SmoothResult result;
    scancore::Mesh smoothed = scancore::Smooth(pScan->mesh, options, &result);
    const double seconds = Seconds(start);
    Log(L"[refine] smooth %d, %d passes, strength %g, keep edges %d: largest move %.4f mm, average %.4f mm, %.1f s",
        c.method, options.passes, options.strength, options.keepOpenEdges ? 1 : 0, result.largestMove,
        result.averageMove, seconds);

    pScan->ReplaceMesh(std::move(smoothed));
    ScanChanged();

    CString status;
    status.Format(L"Smoothed in %.1f s; points moved %.3f mm on average, %.3f mm at most.", seconds,
                  result.averageMove, result.largestMove);
    SetStatus(status);
}

void CRefineDlg::OnUndo()
{
    AFX_MANAGE_STATE(AfxGetStaticModuleState());

    ScanDocument* pScan = FindScan(m_sessionId);
    if (pScan == nullptr || !pScan->CanUndo())
        return;
    CWaitCursor wait;
    pScan->Undo();
    Log(L"[refine] undo: back to %zu triangles", pScan->mesh.triangles.size());
    ScanChanged();
    SetStatus(L"Undone.");
}
