// CompareDialog.cpp : 7 Compare: the scan coloured by its distance from the part.

#include "stdafx.h"
#include "ScanToAlibre.h"
#include "CompareDialog.h"
#include "ScanDocument.h"
#include "ScanImport.h"
#include "ScanViewCommand.h"
#include "Log.h"

#include "Compare.h"

#include <chrono>
#include <cmath>
#include <cwchar>
#include <limits>
#include <vector>

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

namespace
{
    CString Number(double value)
    {
        CString text;
        text.Format(L"%g", value);
        return text;
    }

    // A number from an edit box, or false if it doesn't read as one.
    bool ReadEdit(CWnd* pDialog, int id, double& value)
    {
        CString text;
        pDialog->GetDlgItemText(id, text);
        text.Trim();
        wchar_t* end = nullptr;
        const double read = wcstod(text, &end);
        if (text.IsEmpty() || end == nullptr || *end != L'\0' || !std::isfinite(read))
            return false;
        value = read;
        return true;
    }

    // 812344 as "812,344".
    CString Thousands(size_t value)
    {
        CString digits;
        digits.Format(L"%zu", value);
        CString result;
        const int length = digits.GetLength();
        for (int i = 0; i < length; ++i)
        {
            if (i > 0 && (length - i) % 3 == 0)
                result += L',';
            result += digits[i];
        }
        return result;
    }

    // A signed distance with a real minus sign, such as "+0.134" or "-0.287".
    CString Signed(double value, int decimals)
    {
        CString text;
        text.Format(L"%s%.*f", value < 0.0 ? L"\u2212" : L"+", decimals, std::fabs(value));
        return text;
    }

    // Any numeric SAFEARRAY as doubles.
    bool ReadNumbers(SAFEARRAY* pArray, std::vector<double>& values)
    {
        values.clear();
        if (pArray == nullptr || SafeArrayGetDim(pArray) == 0)
            return false;
        VARTYPE vt = VT_EMPTY;
        SafeArrayGetVartype(pArray, &vt);
        size_t total = 1;
        for (UINT d = 1; d <= SafeArrayGetDim(pArray); ++d)
        {
            LONG low = 0, high = -1;
            SafeArrayGetLBound(pArray, d, &low);
            SafeArrayGetUBound(pArray, d, &high);
            total *= (high >= low) ? static_cast<size_t>(high - low + 1) : 0;
        }
        void* pData = nullptr;
        if (total == 0 || FAILED(SafeArrayAccessData(pArray, &pData)))
            return false;
        values.reserve(total);
        for (size_t k = 0; k < total; ++k)
        {
            double value = std::numeric_limits<double>::quiet_NaN();
            switch (vt)
            {
            case VT_R8: value = static_cast<const double*>(pData)[k]; break;
            case VT_R4: value = static_cast<const float*>(pData)[k]; break;
            case VT_I4: value = static_cast<const LONG*>(pData)[k]; break;
            case VT_VARIANT:
            {
                VARIANT converted;
                VariantInit(&converted);
                if (SUCCEEDED(VariantChangeType(&converted, &static_cast<const VARIANT*>(pData)[k], 0, VT_R8)))
                    value = converted.dblVal;
                VariantClear(&converted);
                break;
            }
            default: break;
            }
            values.push_back(value);
        }
        SafeArrayUnaccessData(pArray);
        return true;
    }

    // The open part's facets (IADPartSession::GetFacetData, unproven on a live
    // session here; CAMForAlibre reads it from parts in an assembly) as a mesh in mm.
    bool ReadPartMesh(const std::wstring& sessionId, scancore::Mesh& part, std::wstring& problem)
    {
        part = scancore::Mesh();
        try
        {
            IADSessionPtr session = theApp.m_pRoot->GetSessions()->GetItem(_variant_t(sessionId.c_str()));
            IADPartSessionPtr partSession = session;
            if (partSession == nullptr)
            {
                problem = L"This isn't a part.";
                return false;
            }
            const auto start = std::chrono::steady_clock::now();
            SAFEARRAY* pFacets = partSession->GetFacetData();
            std::vector<double> values;
            ReadNumbers(pFacets, values);
            if (pFacets != nullptr)
                SafeArrayDestroy(pFacets);
            const double mm = MillimetresPerModelUnit(sessionId);
            part = scancore::MeshFromFacets(values, mm);
            const scancore::Box3 box = scancore::Bounds(part);
            Log(L"[compare] part facets: %zu numbers, %zu triangles, %zu corners; %g mm a unit; box (%.2f, %.2f, %.2f) "
                L"to (%.2f, %.2f, %.2f) mm; %.0f ms",
                values.size(), part.triangles.size(), part.vertices.size(), mm, box.low.x, box.low.y, box.low.z,
                box.high.x, box.high.y, box.high.z,
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
            if (part.triangles.empty())
            {
                problem = L"Nothing to compare: the part has no solid yet.";
                return false;
            }
            return true;
        }
        catch (const _com_error& error)
        {
            Log(L"[compare] reading the part's facets failed: hr=0x%08X", static_cast<unsigned>(error.Error()));
            problem = L"The part's shape couldn't be read (see the log).";
            return false;
        }
    }
}

void CompareViewChanged()
{
    if (CCompareDlg* pDialog = dynamic_cast<CCompareDlg*>(OpenStepDialog()))
        pDialog->UpdateFromScan();
}

BEGIN_MESSAGE_MAP(CCompareDlg, CStepDialog)
    ON_WM_DESTROY()
    ON_WM_DRAWITEM()
    ON_EN_CHANGE(IDC_CMP_TOLERANCE, &CCompareDlg::OnToleranceChanged)
    ON_EN_CHANGE(IDC_CMP_RANGE, &CCompareDlg::OnRangeChanged)
    ON_BN_CLICKED(IDC_CMP_PROBE, &CCompareDlg::OnProbe)
END_MESSAGE_MAP()

CCompareDlg::CCompareDlg(const std::wstring& sessionId)
    : CStepDialog(IDD_COMPARE, sessionId)
{
}

BOOL CCompareDlg::OnInitDialog()
{
    CStepDialog::OnInitDialog();
    m_filling = true;
    if (ScanDocument* pScan = FindScan(m_sessionId))
    {
        pScan->comparing = true;
        pScan->clickMode = ScanDocument::ClickMode::None;
        SetDlgItemText(IDC_CMP_TOLERANCE, Number(pScan->compare.tolerance));
        SetDlgItemText(IDC_CMP_RANGE, Number(pScan->compare.range));
    }
    m_filling = false;
    ShowClickMode();
    ReadPart();
    return TRUE;
}

void CCompareDlg::OnDestroy()
{
    if (ScanDocument* pScan = FindScan(m_sessionId))
    {
        pScan->comparing = false;
        pScan->clickMode = ScanDocument::ClickMode::None;
        pScan->compare.haveProbe = false;
    }
    RefreshScanViews();
    CStepDialog::OnDestroy();
}

void CCompareDlg::ReadPart()
{
    ScanDocument* pScan = FindScan(m_sessionId);
    if (pScan == nullptr)
    {
        UpdateFromScan();
        return;
    }
    SetStatus(L"Reading the part and measuring the scan...");
    CWaitCursor wait;
    pScan->SetMillimetresPerUnit(MillimetresPerModelUnit(m_sessionId));
    std::wstring problem;
    ScanDocument::Compare& c = pScan->compare;
    c.havePart = ReadPartMesh(m_sessionId, c.part, problem);
    pScan->UpdateCompare();
    Recoloured();
    SetStatus(c.havePart ? CString(L"Push Probe, then click the scan to read its distance from the part there.")
                         : CString(problem.c_str()));
}

void CCompareDlg::Recoloured()
{
    RefreshScanViews();
    if (CWnd* pLegend = GetDlgItem(IDC_CMP_LEGEND))
        pLegend->Invalidate();
    UpdateFromScan();
}

void CCompareDlg::UpdateFromScan()
{
    ScanDocument* pScan = FindScan(m_sessionId);
    CString info, stats, probe;
    if (pScan == nullptr)
    {
        info = L"This part has no scan. Load one first.";
    }
    else
    {
        const scancore::Box3& b = pScan->bounds;
        info.Format(L"%s: X %.2f to %.2f, Y %.2f to %.2f, Z %.2f to %.2f mm",
                    ScanDisplayName(pScan->sourceName).c_str(), b.low.x, b.high.x, b.low.y, b.high.y, b.low.z,
                    b.high.z);
        const ScanDocument::Compare& c = pScan->compare;
        const scancore::DeviationStats& s = c.stats;
        const size_t all = s.compared + s.notCompared;
        if (!c.havePart)
            stats = L"Nothing to compare: the part has no solid yet.";
        else if (s.compared == 0)
            stats.Format(L"No point of the scan is within %g mm of the part.", c.range);
        else
            stats.Format(L"Compared %s points (%.0f%%); the rest are more than %g mm from the part (grey). Largest "
                         L"%s / %s mm; RMS %.3f mm; %.1f%% within \u00B1%g mm.",
                         static_cast<LPCWSTR>(Thousands(s.compared)),
                         100.0 * static_cast<double>(s.compared) / static_cast<double>(all), c.range,
                         static_cast<LPCWSTR>(Signed(s.largestPositive, 3)),
                         static_cast<LPCWSTR>(Signed(s.largestNegative, 3)), s.rms, 100.0 * s.within, c.tolerance);
        if (c.haveProbe)
        {
            if (std::isnan(c.probeValue))
                probe.Format(L"At (%.2f, %.2f, %.2f): not modelled (nothing of the part within %g mm).", c.probe.x,
                             c.probe.y, c.probe.z, c.range);
            else
                probe.Format(L"At (%.2f, %.2f, %.2f): %s mm (%s the part).", c.probe.x, c.probe.y, c.probe.z,
                             static_cast<LPCWSTR>(Signed(c.probeValue, 3)),
                             c.probeValue >= 0.0 ? L"outside" : L"inside");
        }
    }
    SetDlgItemText(IDC_CMP_INFO, info);
    SetDlgItemText(IDC_CMP_STATS, stats);
    if (!probe.IsEmpty())
        SetStatus(probe);
    GetDlgItem(IDC_CMP_PROBE)->EnableWindow(pScan != nullptr && pScan->compare.havePart);
}

void CCompareDlg::SetStatus(const CString& text)
{
    SetDlgItemText(IDC_CMP_STATUS, text);
    GetDlgItem(IDC_CMP_STATUS)->UpdateWindow();
}

void CCompareDlg::ShowClickMode()
{
    const ScanDocument* pScan = FindScan(m_sessionId);
    CheckDlgButton(IDC_CMP_PROBE,
                   pScan != nullptr && pScan->clickMode == ScanDocument::ClickMode::Probe ? BST_CHECKED : BST_UNCHECKED);
}

void CCompareDlg::OnProbe()
{
    ScanDocument* pScan = FindScan(m_sessionId);
    if (pScan == nullptr)
        return;
    using Mode = ScanDocument::ClickMode;
    pScan->clickMode = (pScan->clickMode == Mode::Probe) ? Mode::None : Mode::Probe;
    ShowClickMode();
    SetStatus(pScan->clickMode == Mode::Probe ? L"Click the scan to read its distance from the part there."
                                              : L"Clicks in the view are Alibre's.");
}

void CCompareDlg::OnToleranceChanged()
{
    ScanDocument* pScan = FindScan(m_sessionId);
    double value = 0.0;
    if (m_filling || pScan == nullptr || !ReadEdit(this, IDC_CMP_TOLERANCE, value))
        return;
    if (value < 0.001 || value >= pScan->compare.range)
    {
        SetStatus(L"The tolerance must be at least 0.001 mm and less than the range.");
        return;
    }
    if (value == pScan->compare.tolerance)
        return;
    pScan->compare.tolerance = value;
    CWaitCursor wait;
    pScan->RebandCompare();
    Recoloured();
    SetStatus(L"");
}

void CCompareDlg::OnRangeChanged()
{
    ScanDocument* pScan = FindScan(m_sessionId);
    double value = 0.0;
    if (m_filling || pScan == nullptr || !ReadEdit(this, IDC_CMP_RANGE, value))
        return;
    if (value <= pScan->compare.tolerance || value > 100.0)
    {
        SetStatus(L"The range must be more than the tolerance, and at most 100 mm.");
        return;
    }
    if (value == pScan->compare.range)
        return;
    pScan->compare.range = value;
    CWaitCursor wait;
    // A wider range needs a wider search; a narrower one just greys more.
    if (pScan->compare.havePart && value > pScan->compare.searched)
        pScan->UpdateCompare();
    else
        pScan->RebandCompare();
    Recoloured();
    SetStatus(L"");
}

void CCompareDlg::OnDrawItem(int id, LPDRAWITEMSTRUCT pDraw)
{
    if (id != IDC_CMP_LEGEND || pDraw == nullptr)
    {
        CStepDialog::OnDrawItem(id, pDraw);
        return;
    }
    CDC* pDC = CDC::FromHandle(pDraw->hDC);
    const CRect all(pDraw->rcItem);
    pDC->FillSolidRect(all, GetSysColor(COLOR_BTNFACE));
    const ScanDocument* pScan = FindScan(m_sessionId);
    const double tolerance = pScan != nullptr ? pScan->compare.tolerance : 0.1;
    const double range = pScan != nullptr ? pScan->compare.range : 1.0;

    // Seven coloured swatches, then a gap and grey; the band edges below.
    const int bandsWide = scancore::kDeviationBands - 1;
    const int gap = all.Width() / 40;
    const double width = (all.Width() - gap) / static_cast<double>(scancore::kDeviationBands);
    const int swatchBottom = all.top + all.Height() / 2;
    for (int k = 0; k < scancore::kDeviationBands; ++k)
    {
        long rgb[3];
        DeviationColour(k, rgb);
        const int left = all.left + static_cast<int>(k * width) + (k == bandsWide ? gap : 0);
        const int right = all.left + static_cast<int>((k + 1) * width) + (k == bandsWide ? gap : 0);
        pDC->FillSolidRect(CRect(left, all.top, right, swatchBottom), RGB(rgb[0], rgb[1], rgb[2]));
    }
    CFont* pOldFont = pDC->SelectObject(GetFont());
    pDC->SetBkMode(TRANSPARENT);
    pDC->SetTextColor(GetSysColor(COLOR_BTNTEXT));
    const double edges[8] = { -range, -2.0 * range / 3.0, -range / 3.0, -tolerance,
                              tolerance, range / 3.0, 2.0 * range / 3.0, range };
    for (int k = 0; k <= bandsWide; ++k)
    {
        CString label;
        label.Format(L"%s%.2f", edges[k] < 0.0 ? L"\u2212" : L"", std::fabs(edges[k]));
        const int x = all.left + static_cast<int>(k * width);
        CRect box(x - static_cast<int>(width / 2), swatchBottom + 1, x + static_cast<int>(width / 2), all.bottom);
        UINT format = DT_CENTER;
        if (k == 0)
        {
            box.left = x;
            box.right = x + static_cast<int>(width);
            format = DT_LEFT;
        }
        else if (k == bandsWide)
        {
            box.left = x - static_cast<int>(width);
            box.right = x;
            format = DT_RIGHT;
        }
        pDC->DrawText(label, box, format | DT_SINGLELINE | DT_NOPREFIX);
    }
    CRect noneBox(all.left + static_cast<int>(bandsWide * width) + gap, swatchBottom + 1, all.right, all.bottom);
    pDC->DrawText(L"none", noneBox, DT_CENTER | DT_SINGLELINE | DT_NOPREFIX);
    pDC->SelectObject(pOldFont);
}
