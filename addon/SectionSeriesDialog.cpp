// SectionSeriesDialog.cpp : Section Series: slice the scan at a series of
// offsets, match the slices for lofting, and make a sketch at each.

#include "stdafx.h"
#include "ScanToAlibre.h"
#include "SectionSeriesDialog.h"
#include "ScanDocument.h"
#include "ScanImport.h"
#include "ScanViewCommand.h"
#include "SketchWriter.h"
#include "Log.h"

#include <cmath>
#include <cwchar>

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

namespace
{
    // The Round radii to choices, as in Section.
    const double kRoundSteps[3] = { 0.1, 0.01, 0.0 };

    CString Number(double value)
    {
        CString text;
        text.Format(L"%g", value);
        return text;
    }

    CString Wide(const std::string& text)
    {
        return CString(std::wstring(text.begin(), text.end()).c_str());
    }

    bool ReadNumber(const CString& input, double& value)
    {
        CString text = input;
        text.Trim();
        wchar_t* end = nullptr;
        const double read = wcstod(text, &end);
        if (text.IsEmpty() || end == nullptr || *end != L'\0' || !std::isfinite(read))
            return false;
        value = read;
        return true;
    }

    bool ReadEdit(CWnd* pDialog, int id, double& value)
    {
        CString text;
        pDialog->GetDlgItemText(id, text);
        return ReadNumber(text, value);
    }

    const UINT kResliceMessage = WM_APP + 1;

    const wchar_t* Plural(size_t n)
    {
        return n == 1 ? L"" : L"s";
    }
}

void SeriesViewChanged()
{
    if (CSectionSeriesDlg* pDialog = dynamic_cast<CSectionSeriesDlg*>(OpenStepDialog()))
        pDialog->UpdateFromScan();
}

void SeriesPlanePicked()
{
    if (CSectionSeriesDlg* pDialog = dynamic_cast<CSectionSeriesDlg*>(OpenStepDialog()))
        pDialog->PlanePicked();
}

BEGIN_MESSAGE_MAP(CSectionSeriesDlg, CStepDialog)
    ON_WM_DESTROY()
    ON_BN_CLICKED(IDC_SEC_PICK_ALIBRE, &CSectionSeriesDlg::OnPickAlibre)
    ON_BN_CLICKED(IDC_SEC_PICK_SCAN, &CSectionSeriesDlg::OnPickScan)
    ON_EN_CHANGE(IDC_SER_FIRST, &CSectionSeriesDlg::OnRangeChanged)
    ON_EN_CHANGE(IDC_SER_LAST, &CSectionSeriesDlg::OnRangeChanged)
    ON_EN_CHANGE(IDC_SER_COUNT, &CSectionSeriesDlg::OnRangeChanged)
    ON_BN_CLICKED(IDC_SER_MAKE, &CSectionSeriesDlg::OnMake)
    ON_BN_CLICKED(IDC_SER_ADD, &CSectionSeriesDlg::OnAdd)
    ON_BN_CLICKED(IDC_SER_DELETE, &CSectionSeriesDlg::OnDelete)
    ON_BN_CLICKED(IDC_SER_LOFT, &CSectionSeriesDlg::OnLoftChanged)
    ON_BN_CLICKED(IDC_SER_SPLINES, &CSectionSeriesDlg::OnLoftChanged)
    ON_BN_CLICKED(IDC_SER_LINES_ARCS, &CSectionSeriesDlg::OnLoftChanged)
    ON_BN_CLICKED(IDC_SER_GUIDES, &CSectionSeriesDlg::OnLoftChanged)
    ON_EN_CHANGE(IDC_SEC_TOLERANCE, &CSectionSeriesDlg::OnToleranceChanged)
    ON_BN_CLICKED(IDC_SEC_HV, &CSectionSeriesDlg::OnIntentChanged)
    ON_BN_CLICKED(IDC_SEC_TANGENT, &CSectionSeriesDlg::OnIntentChanged)
    ON_BN_CLICKED(IDC_SEC_CONCENTRIC, &CSectionSeriesDlg::OnIntentChanged)
    ON_CBN_SELCHANGE(IDC_SEC_ROUND, &CSectionSeriesDlg::OnIntentChanged)
    ON_BN_CLICKED(IDC_SEC_CREATE_RAW, &CSectionSeriesDlg::OnSketchOptionsChanged)
    ON_BN_CLICKED(IDC_SEC_CREATE_FITTED, &CSectionSeriesDlg::OnSketchOptionsChanged)
    ON_BN_CLICKED(IDC_SEC_FIT_ARCS, &CSectionSeriesDlg::OnSketchOptionsChanged)
    ON_BN_CLICKED(IDC_SER_CREATE, &CSectionSeriesDlg::OnCreate)
    ON_NOTIFY(LVN_ITEMCHANGED, IDC_SER_LIST, &CSectionSeriesDlg::OnListItemChanged)
    ON_NOTIFY(NM_DBLCLK, IDC_SER_LIST, &CSectionSeriesDlg::OnListDoubleClick)
    ON_NOTIFY(LVN_KEYDOWN, IDC_SER_LIST, &CSectionSeriesDlg::OnListKeyDown)
    ON_NOTIFY(LVN_BEGINLABELEDIT, IDC_SER_LIST, &CSectionSeriesDlg::OnListBeginEdit)
    ON_NOTIFY(LVN_ENDLABELEDIT, IDC_SER_LIST, &CSectionSeriesDlg::OnListEndEdit)
    ON_MESSAGE(kResliceMessage, &CSectionSeriesDlg::OnResliceLater)
END_MESSAGE_MAP()

CSectionSeriesDlg::CSectionSeriesDlg(const std::wstring& sessionId)
    : CStepDialog(IDD_SECTION_SERIES, sessionId)
{
}

BOOL CSectionSeriesDlg::OnInitDialog()
{
    CStepDialog::OnInitDialog();

    m_round.SubclassDlgItem(IDC_SEC_ROUND, this);
    m_round.AddString(L"0.1 mm");
    m_round.AddString(L"0.01 mm");
    m_round.AddString(L"Off");

    m_list.SubclassDlgItem(IDC_SER_LIST, this);
    m_list.SetExtendedStyle(LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
    m_list.InsertColumn(0, L"Offset (mm)", LVCFMT_RIGHT, 70);
    m_list.InsertColumn(1, L"Plane", LVCFMT_RIGHT, 40);
    m_list.InsertColumn(2, L"Loops", LVCFMT_LEFT, 70);
    m_list.InsertColumn(3, L"Fit", LVCFMT_LEFT, 120);

    m_filling = true;
    if (ScanDocument* pScan = FindScan(m_sessionId))
    {
        pScan->seriesing = true;
        pScan->clickMode = ScanDocument::ClickMode::None;
        const ScanDocument::Series& s = pScan->series;
        SetDlgItemText(IDC_SER_FIRST, Number(s.first));
        SetDlgItemText(IDC_SER_LAST, Number(s.last));
        SetDlgItemInt(IDC_SER_COUNT, static_cast<UINT>(s.count), FALSE);
        SetDlgItemText(IDC_SEC_TOLERANCE, Number(s.plane.tolerance));
        CheckDlgButton(IDC_SEC_HV, s.plane.snap.horizontalVertical ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(IDC_SEC_TANGENT, s.plane.snap.tangent ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(IDC_SEC_CONCENTRIC, s.plane.snap.concentric ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(IDC_SEC_CREATE_RAW, s.plane.createRaw ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(IDC_SEC_CREATE_FITTED, s.plane.createFitted ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(IDC_SEC_FIT_ARCS, s.plane.snap.fitArcs ? BST_CHECKED : BST_UNCHECKED);
        int round = 2;
        for (int k = 0; k < 3; ++k)
            if (std::fabs(s.plane.snap.roundTo - kRoundSteps[k]) < 1e-12)
                round = k;
        m_round.SetCurSel(round);
        // The scan may have moved or changed since the last series.
        CWaitCursor wait;
        pScan->UpdateSeries();
    }
    m_filling = false;
    ShowOptions();
    RefreshScanViews();
    UpdateFromScan();
    ShowClickMode();
    SetStatus(L"Pick Alibre Plane or Pick Scan Plane, then click the plane the series starts from.");
    return TRUE;
}

void CSectionSeriesDlg::OnDestroy()
{
    if (ScanDocument* pScan = FindScan(m_sessionId))
    {
        pScan->seriesing = false;
        pScan->clickMode = ScanDocument::ClickMode::None;
        pScan->chosen = -1;
    }
    RefreshScanViews();
    CStepDialog::OnDestroy();
}

void CSectionSeriesDlg::ShowOptions()
{
    const ScanDocument* pScan = FindScan(m_sessionId);
    if (pScan == nullptr)
        return;
    const scancore::SeriesOptions& o = pScan->series.options;
    m_filling = true;
    CheckDlgButton(IDC_SER_LOFT, o.loft ? BST_CHECKED : BST_UNCHECKED);
    CheckRadioButton(IDC_SER_SPLINES, IDC_SER_LINES_ARCS, o.splines ? IDC_SER_SPLINES : IDC_SER_LINES_ARCS);
    CheckDlgButton(IDC_SER_GUIDES, o.guides ? BST_CHECKED : BST_UNCHECKED);
    GetDlgItem(IDC_SER_SPLINES)->EnableWindow(o.loft);
    GetDlgItem(IDC_SER_LINES_ARCS)->EnableWindow(o.loft);
    // Guides need matched points.
    GetDlgItem(IDC_SER_GUIDES)->EnableWindow(o.loft);
    // Frank: with Create Loft the button makes the loft too, and says so.
    SetDlgItemText(IDC_SER_CREATE, o.loft ? L"Create Loft" : L"Create Sketches");
    m_filling = false;
}

void CSectionSeriesDlg::PlanePicked()
{
    ScanDocument* pScan = FindScan(m_sessionId);
    ShowClickMode();
    if (pScan != nullptr && pScan->series.rangeFromPick)
    {
        // A new plane: the range and list were made for it.
        pScan->series.rangeFromPick = false;
        m_filling = true;
        SetDlgItemText(IDC_SER_FIRST, Number(pScan->series.first));
        SetDlgItemText(IDC_SER_LAST, Number(pScan->series.last));
        m_filling = false;
    }
    UpdateFromScan();
    SetStatus(L"Plane chosen, and the list made across the scan. Edit the offsets, or Create Sketches.");
}

void CSectionSeriesDlg::FillList()
{
    const ScanDocument* pScan = FindScan(m_sessionId);
    m_filling = true;
    m_list.DeleteAllItems();
    if (pScan != nullptr)
    {
        const ScanDocument::Series& s = pScan->series;
        const scancore::SeriesResult& r = s.result;
        const bool sliced = r.slices.size() == s.offsets.size();
        // Per slice, what the loft fit makes.
        std::vector<size_t> points(s.offsets.size(), 0), segments(s.offsets.size(), 0), splines(s.offsets.size(), 0);
        for (const scancore::SeriesChain& chain : r.chains)
            for (size_t k = 0; k < chain.polyline.size(); ++k)
            {
                const size_t row = chain.firstSlice + k;
                if (row >= points.size())
                    continue;
                if (chain.splines)
                {
                    ++splines[row];
                    points[row] += chain.spline[k].size();
                }
                else
                {
                    segments[row] += chain.profiles[k].segments.size();
                }
            }
        for (size_t row = 0; row < s.offsets.size(); ++row)
        {
            const int item = m_list.InsertItem(static_cast<int>(row), Number(s.offsets[row]));
            m_list.SetItemData(item, row);
            CString plane;
            plane.Format(L"%zu", row + 1);
            m_list.SetItemText(item, 1, plane);
            if (!sliced)
                continue;
            const scancore::SeriesSlice& slice = r.slices[row];
            CString loops;
            if (slice.section.slices.empty())
                loops = L"misses";
            else if (slice.open == 0)
                loops.Format(L"%zu", slice.loops);
            else
                loops.Format(L"%zu + %zu open", slice.loops, slice.open);
            m_list.SetItemText(item, 2, loops);
            CString fit;
            if (!s.options.loft)
            {
                size_t n = 0;
                for (const scancore::Profile& p : slice.section.profiles)
                    n += p.segments.size();
                fit.Format(L"%zu segment%s", n, Plural(n));
            }
            else if (splines[row] > 0 && segments[row] > 0)
                fit.Format(L"%zu spline points, %zu segments", points[row], segments[row]);
            else if (splines[row] > 0)
                fit.Format(L"%zu spline%s, %zu points", splines[row], Plural(splines[row]), points[row]);
            else if (segments[row] > 0)
                fit.Format(L"%zu segment%s", segments[row], Plural(segments[row]));
            m_list.SetItemText(item, 3, fit);
        }
        if (s.chosenRow >= 0 && s.chosenRow < m_list.GetItemCount())
        {
            m_list.SetItemState(s.chosenRow, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
            m_list.EnsureVisible(s.chosenRow, FALSE);
        }
    }
    m_filling = false;
}

void CSectionSeriesDlg::UpdateFromScan()
{
    ScanDocument* pScan = FindScan(m_sessionId);
    CString info, plane, result;
    bool canCreate = false;
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
        const ScanDocument::Series& s = pScan->series;
        if (!s.plane.havePlane)
        {
            plane = L"(none yet)";
        }
        else
        {
            plane.Format(L"%s%s", static_cast<LPCWSTR>(Wide(s.plane.planeName)),
                         s.plane.alibrePlane != nullptr
                             ? (s.plane.alibreFace ? L" (Alibre face)" : L" (Alibre plane)")
                             : L" (scan plane)");
            const scancore::SeriesResult& r = s.result;
            if (r.slices.empty())
            {
                result = s.invalid.empty() ? CString(L"Press Make to fill the list.") : Wide(s.invalid);
            }
            else
            {
                size_t missed = 0, scraps = 0;
                for (const scancore::SeriesSlice& slice : r.slices)
                {
                    missed += slice.section.slices.empty() ? 1 : 0;
                    scraps += slice.scraps;
                }
                result.Format(L"%zu plane%s: %s.", r.slices.size(), Plural(r.slices.size()),
                              static_cast<LPCWSTR>(Wide(scancore::DescribeRuns(r))));
                if (s.options.loft)
                {
                    double worst = 0.0;
                    for (const scancore::SeriesChain& chain : r.chains)
                        worst = (std::max)(worst, chain.largestDeviation);
                    size_t guides = 0;
                    for (const scancore::SeriesChain& chain : r.chains)
                        guides += chain.guides.size();
                    CString more;
                    more.Format(L" %zu shape%s followed, within %.3f mm of the slices; %zu guide curve%s.",
                                r.chains.size(), Plural(r.chains.size()), worst, guides, Plural(guides));
                    result += more;
                }
                if (missed > 0 || r.openPolylines > 0 || scraps > 0)
                {
                    CString more;
                    more.Format(L" %zu plane%s miss the scan; %zu open polyline%s and %zu tiny loop%s are written "
                                L"plain.",
                                missed, Plural(missed), r.openPolylines, Plural(r.openPolylines), scraps,
                                Plural(scraps));
                    result += more;
                }
                if (!r.notes.empty())
                {
                    result += L" ";
                    result += Wide(r.notes.front());
                    if (r.notes.size() > 1)
                    {
                        CString more;
                        more.Format(L" (and %zu more in the log)", r.notes.size() - 1);
                        result += more;
                    }
                    result += L".";
                }
                canCreate = missed < r.slices.size();
            }
        }
    }
    SetDlgItemText(IDC_SEC_INFO, info);
    SetDlgItemText(IDC_SEC_PLANE, plane);
    SetDlgItemText(IDC_SER_RESULT, result);
    if (pScan != nullptr && !pScan->series.plane.createRaw && !pScan->series.plane.createFitted)
        canCreate = false;
    GetDlgItem(IDC_SER_CREATE)->EnableWindow(canCreate);
    GetDlgItem(IDC_SEC_PICK_ALIBRE)->EnableWindow(pScan != nullptr);
    GetDlgItem(IDC_SEC_PICK_SCAN)->EnableWindow(pScan != nullptr);
    const bool havePlane = pScan != nullptr && pScan->series.plane.havePlane;
    GetDlgItem(IDC_SER_MAKE)->EnableWindow(havePlane);
    GetDlgItem(IDC_SER_ADD)->EnableWindow(havePlane);
    GetDlgItem(IDC_SER_DELETE)->EnableWindow(havePlane && m_list.GetSelectedCount() > 0);
    FillList();
}

void CSectionSeriesDlg::SetStatus(const CString& text)
{
    SetDlgItemText(IDC_SER_STATUS, text);
    GetDlgItem(IDC_SER_STATUS)->UpdateWindow();
}

void CSectionSeriesDlg::ClearAlibreSelection()
{
    if (theApp.m_pRoot == nullptr)
        return;
    try
    {
        IADSessionPtr session = theApp.m_pRoot->GetSessions()->GetItem(_variant_t(m_sessionId.c_str()));
        if (session != nullptr)
            session->Select(theApp.m_pRoot->NewObjectCollector());
    }
    catch (const _com_error& error)
    {
        Log(L"[series] clearing Alibre's selection failed: hr=0x%08X", static_cast<unsigned>(error.Error()));
    }
}

void CSectionSeriesDlg::OnPickAlibre()
{
    ToggleClickMode(static_cast<int>(ScanDocument::ClickMode::Target));
}

void CSectionSeriesDlg::OnPickScan()
{
    ToggleClickMode(static_cast<int>(ScanDocument::ClickMode::Scan));
}

void CSectionSeriesDlg::ToggleClickMode(int mode)
{
    ScanDocument* pScan = FindScan(m_sessionId);
    if (pScan == nullptr)
        return;
    using Mode = ScanDocument::ClickMode;
    const Mode pushed = static_cast<Mode>(mode);
    pScan->clickMode = (pScan->clickMode == pushed) ? Mode::None : pushed;
    if (pScan->clickMode == Mode::Target)
        ClearAlibreSelection();     // so clicking the plane still selected is a change
    ShowClickMode();
    RefreshScanViews();
    switch (pScan->clickMode)
    {
    case Mode::Target:
        SetStatus(L"Click one of Alibre's design planes or a flat face of the part.");
        break;
    case Mode::Scan:
        SetStatus(L"Click one of the scan's planes: its box's centre planes or a plane fitted in Orient.");
        break;
    default:
        SetStatus(L"Clicks in the view are Alibre's.");
        break;
    }
}

void CSectionSeriesDlg::ShowClickMode()
{
    const ScanDocument* pScan = FindScan(m_sessionId);
    const ScanDocument::ClickMode mode = (pScan != nullptr) ? pScan->clickMode : ScanDocument::ClickMode::None;
    CheckDlgButton(IDC_SEC_PICK_ALIBRE, mode == ScanDocument::ClickMode::Target ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(IDC_SEC_PICK_SCAN, mode == ScanDocument::ClickMode::Scan ? BST_CHECKED : BST_UNCHECKED);
}

void CSectionSeriesDlg::Reslice()
{
    ScanDocument* pScan = FindScan(m_sessionId);
    if (pScan == nullptr)
        return;
    CWaitCursor wait;
    pScan->UpdateSeries();
    RefreshScanViews();
    UpdateFromScan();
}

void CSectionSeriesDlg::OnRangeChanged()
{
    ScanDocument* pScan = FindScan(m_sessionId);
    if (m_filling || pScan == nullptr)
        return;
    double value = 0.0;
    if (ReadEdit(this, IDC_SER_FIRST, value))
        pScan->series.first = value;
    if (ReadEdit(this, IDC_SER_LAST, value))
        pScan->series.last = value;
    BOOL ok = FALSE;
    const UINT count = GetDlgItemInt(IDC_SER_COUNT, &ok, FALSE);
    if (ok)
        pScan->series.count = static_cast<int>(count);
}

void CSectionSeriesDlg::OnMake()
{
    ScanDocument* pScan = FindScan(m_sessionId);
    if (pScan == nullptr)
        return;
    double first = 0.0, last = 0.0;
    BOOL ok = FALSE;
    const UINT count = GetDlgItemInt(IDC_SER_COUNT, &ok, FALSE);
    if (!ReadEdit(this, IDC_SER_FIRST, first) || !ReadEdit(this, IDC_SER_LAST, last) || !ok)
    {
        SetStatus(L"First and Last must be numbers, and Count a whole number.");
        return;
    }
    if (count < 1 || count > 200)
    {
        SetStatus(L"Count must be from 1 to 200.");
        return;
    }
    if (count > 1 && !(last > first))
    {
        SetStatus(L"Last must be past First.");
        return;
    }
    ScanDocument::Series& s = pScan->series;
    s.first = first;
    s.last = last;
    s.count = static_cast<int>(count);
    s.offsets = scancore::EvenOffsets(first, last, s.count);
    s.chosenRow = -1;
    SetStatus(L"");
    Reslice();
}

void CSectionSeriesDlg::OnAdd()
{
    ScanDocument* pScan = FindScan(m_sessionId);
    if (pScan == nullptr)
        return;
    ScanDocument::Series& s = pScan->series;
    std::vector<double>& o = s.offsets;
    // Halfway between the chosen row and the next; after the last (or with
    // nothing chosen), as far past the last as the last gap.
    const int chosen = m_list.GetNextItem(-1, LVNI_SELECTED);
    size_t at = o.size();
    double value = 0.0;
    if (o.empty())
        value = s.first;
    else if (chosen >= 0 && static_cast<size_t>(chosen) + 1 < o.size())
    {
        at = static_cast<size_t>(chosen) + 1;
        value = 0.5 * (o[chosen] + o[at]);
    }
    else
    {
        const double gap = o.size() >= 2 ? o.back() - o[o.size() - 2] : 1.0;
        value = o.back() + gap;
    }
    value = scancore::RoundTo(value, 0.001);
    o.insert(o.begin() + static_cast<std::ptrdiff_t>(at), value);
    s.chosenRow = static_cast<int>(at);
    CString status;
    status.Format(L"Plane %zu added at %g mm.", at + 1, value);
    SetStatus(status);
    Reslice();
}

void CSectionSeriesDlg::OnDelete()
{
    ScanDocument* pScan = FindScan(m_sessionId);
    if (pScan == nullptr)
        return;
    std::vector<double>& o = pScan->series.offsets;
    std::vector<bool> remove(o.size(), false);
    size_t removed = 0;
    for (int item = m_list.GetNextItem(-1, LVNI_SELECTED); item >= 0; item = m_list.GetNextItem(item, LVNI_SELECTED))
    {
        const size_t row = static_cast<size_t>(m_list.GetItemData(item));
        if (row < remove.size() && !remove[row])
        {
            remove[row] = true;
            ++removed;
        }
    }
    if (removed == 0)
        return;
    std::vector<double> kept;
    for (size_t k = 0; k < o.size(); ++k)
        if (!remove[k])
            kept.push_back(o[k]);
    o = kept;
    pScan->series.chosenRow = -1;
    CString status;
    status.Format(L"%zu plane%s deleted.", removed, Plural(removed));
    SetStatus(status);
    Reslice();
}

void CSectionSeriesDlg::OnLoftChanged()
{
    ScanDocument* pScan = FindScan(m_sessionId);
    if (m_filling || pScan == nullptr)
        return;
    scancore::SeriesOptions& o = pScan->series.options;
    o.loft = IsDlgButtonChecked(IDC_SER_LOFT) == BST_CHECKED;
    o.splines = IsDlgButtonChecked(IDC_SER_SPLINES) == BST_CHECKED;
    o.guides = IsDlgButtonChecked(IDC_SER_GUIDES) == BST_CHECKED;
    ShowOptions();
    Reslice();
}

void CSectionSeriesDlg::OnToleranceChanged()
{
    ScanDocument* pScan = FindScan(m_sessionId);
    double value = 0.0;
    if (m_filling || pScan == nullptr || !ReadEdit(this, IDC_SEC_TOLERANCE, value))
        return;
    if (value < 0.001 || value > 5.0)
    {
        SetStatus(L"The tolerance must be from 0.001 to 5 mm.");
        return;
    }
    ScanDocument::Section& plane = pScan->series.plane;
    if (value == plane.tolerance)
        return;
    plane.tolerance = value;
    plane.snap.concentricDistance = value;
    SetStatus(L"");
    Reslice();
}

void CSectionSeriesDlg::OnIntentChanged()
{
    ScanDocument* pScan = FindScan(m_sessionId);
    if (m_filling || pScan == nullptr)
        return;
    // The design intent is worked out when the sketches are made, in each
    // sketch's own axes, so nothing is fitted again.
    scancore::SnapOptions& snap = pScan->series.plane.snap;
    snap.horizontalVertical = IsDlgButtonChecked(IDC_SEC_HV) == BST_CHECKED;
    snap.tangent = IsDlgButtonChecked(IDC_SEC_TANGENT) == BST_CHECKED;
    snap.concentric = IsDlgButtonChecked(IDC_SEC_CONCENTRIC) == BST_CHECKED;
    const int round = m_round.GetCurSel();
    snap.roundTo = (round >= 0 && round < 3) ? kRoundSteps[round] : 0.0;
}

void CSectionSeriesDlg::OnSketchOptionsChanged()
{
    ScanDocument* pScan = FindScan(m_sessionId);
    if (m_filling || pScan == nullptr)
        return;
    ScanDocument::Section& plane = pScan->series.plane;
    plane.createRaw = IsDlgButtonChecked(IDC_SEC_CREATE_RAW) == BST_CHECKED;
    plane.createFitted = IsDlgButtonChecked(IDC_SEC_CREATE_FITTED) == BST_CHECKED;
    const bool arcs = IsDlgButtonChecked(IDC_SEC_FIT_ARCS) == BST_CHECKED;
    if (arcs != plane.snap.fitArcs)
    {
        plane.snap.fitArcs = arcs;
        Reslice();
    }
    else
    {
        UpdateFromScan();
    }
    SetStatus(!plane.createRaw && !plane.createFitted ? L"Tick Create Raw Sketch or Create Fitted Sketch to make sketches."
                                                      : L"");
}

void CSectionSeriesDlg::OnListItemChanged(NMHDR* pNotify, LRESULT* pResult)
{
    *pResult = 0;
    const NMLISTVIEW* pView = reinterpret_cast<NMLISTVIEW*>(pNotify);
    ScanDocument* pScan = FindScan(m_sessionId);
    if (m_filling || pScan == nullptr || (pView->uChanged & LVIF_STATE) == 0)
        return;
    const int chosen = m_list.GetNextItem(-1, LVNI_SELECTED);
    const int row = chosen >= 0 ? static_cast<int>(m_list.GetItemData(chosen)) : -1;
    GetDlgItem(IDC_SER_DELETE)->EnableWindow(pScan->series.plane.havePlane && m_list.GetSelectedCount() > 0);
    if (row != pScan->series.chosenRow)
    {
        pScan->series.chosenRow = row;
        RefreshScanViews();
    }
}

void CSectionSeriesDlg::OnListDoubleClick(NMHDR* pNotify, LRESULT* pResult)
{
    *pResult = 0;
    const NMITEMACTIVATE* pItem = reinterpret_cast<NMITEMACTIVATE*>(pNotify);
    if (pItem->iItem >= 0)
    {
        m_list.SetFocus();
        m_list.EditLabel(pItem->iItem);
    }
}

void CSectionSeriesDlg::OnListKeyDown(NMHDR* pNotify, LRESULT* pResult)
{
    *pResult = 0;
    const NMLVKEYDOWN* pKey = reinterpret_cast<NMLVKEYDOWN*>(pNotify);
    if (pKey->wVKey == VK_F2)
    {
        const int item = m_list.GetNextItem(-1, LVNI_FOCUSED);
        if (item >= 0)
            m_list.EditLabel(item);
    }
}

void CSectionSeriesDlg::OnListBeginEdit(NMHDR* /*pNotify*/, LRESULT* pResult)
{
    *pResult = FALSE;   // allow it
}

void CSectionSeriesDlg::OnListEndEdit(NMHDR* pNotify, LRESULT* pResult)
{
    *pResult = FALSE;
    const NMLVDISPINFO* pInfo = reinterpret_cast<NMLVDISPINFO*>(pNotify);
    ScanDocument* pScan = FindScan(m_sessionId);
    if (pScan == nullptr || pInfo->item.pszText == nullptr)
        return;     // cancelled
    const size_t row = static_cast<size_t>(m_list.GetItemData(pInfo->item.iItem));
    std::vector<double> offsets = pScan->series.offsets;
    double value = 0.0;
    if (row >= offsets.size() || !ReadNumber(pInfo->item.pszText, value))
    {
        SetStatus(L"The offset must be a number of millimetres; the old one stays.");
        MessageBeep(MB_ICONWARNING);
        return;
    }
    offsets[row] = value;
    std::string message;
    if (!scancore::ValidateOffsets(offsets, message))
    {
        CString status;
        status.Format(L"%g mm is refused: offsets must go up from row to row, so plane %zu must be past %s and "
                      L"before %s. The old one stays.",
                      value, row + 1,
                      row > 0 ? static_cast<LPCWSTR>(Number(pScan->series.offsets[row - 1])) : L"nothing",
                      row + 1 < offsets.size() ? static_cast<LPCWSTR>(Number(pScan->series.offsets[row + 1]))
                                               : L"nothing");
        SetStatus(status);
        MessageBeep(MB_ICONWARNING);
        return;
    }
    pScan->series.offsets = offsets;
    pScan->series.chosenRow = static_cast<int>(row);
    CString status;
    status.Format(L"Plane %zu moved to %g mm.", row + 1, value);
    SetStatus(status);
    // Sliced again (and the list filled again) after this notice returns, not
    // while the list is still ending its edit.
    PostMessage(kResliceMessage);
}

LRESULT CSectionSeriesDlg::OnResliceLater(WPARAM, LPARAM)
{
    Reslice();
    return 0;
}

void CSectionSeriesDlg::OnCreate()
{
    AFX_MANAGE_STATE(AfxGetStaticModuleState());
    ScanDocument* pScan = FindScan(m_sessionId);
    if (pScan == nullptr || !pScan->series.plane.havePlane)
        return;
    // Picking ends, so the next press of a Pick button starts a new pick.
    pScan->clickMode = ScanDocument::ClickMode::None;
    ShowClickMode();

    // Frank: warn when neighbouring sections don't overlap, as the loft between
    // them is likely to fold through itself (ACIS's invalid body), and recommend
    // removing the small section or adding planes so the shape changes slowly.
    const scancore::SeriesResult& r = pScan->series.result;
    if (pScan->series.options.loft && pScan->series.plane.createFitted && !r.gaps.empty())
    {
        CString pairs;
        size_t smallRow = 0, firstRow = 0;
        for (size_t k = 0; k < r.gaps.size() && k < 6; ++k)
        {
            const scancore::SeriesGap& g = r.gaps[k];
            const bool firstSmaller = g.firstArea < g.secondArea;
            const double ratio = firstSmaller ? g.secondArea / (std::max)(g.firstArea, 1e-9)
                                              : g.firstArea / (std::max)(g.secondArea, 1e-9);
            CString line;
            line.Format(L"\n  - planes %zu and %zu: plane %zu's section is %.1f times smaller", g.first + 1,
                        g.second + 1, (firstSmaller ? g.first : g.second) + 1, ratio);
            pairs += line;
            if (k == 0)
            {
                smallRow = (firstSmaller ? g.first : g.second) + 1;
                firstRow = g.first + 1;
            }
        }
        if (r.gaps.size() > 6)
            pairs += L"\n  - and more (see the log)";
        for (const scancore::SeriesGap& g : r.gaps)
            Log(L"[series] planes %zu and %zu don't overlap (sections %.2f and %.2f mm^2)", g.first + 1, g.second + 1,
                g.firstArea, g.secondArea);
        CString text;
        text.Format(L"These neighbouring sections don't overlap seen along the series, so the loft between them is "
                    L"likely to fold through itself and fail (\"The Feature results in an invalid Body\"):\n%s\n\n"
                    L"Either remove the small section (choose plane %zu and press Delete), or add more planes between "
                    L"them so the shape changes slowly (choose plane %zu and press Add, more than once if need be).\n\n"
                    L"Make the loft anyway?",
                    static_cast<LPCWSTR>(pairs), smallRow, firstRow);
        if (MessageBox(text, L"Section Series", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES)
        {
            CString status;
            status.Format(L"No loft made: remove plane %zu, or add planes after plane %zu, then press Create Loft.",
                          smallRow, firstRow);
            SetStatus(status);
            return;
        }
    }

    SetStatus(pScan->series.options.loft ? L"Making the sketches, guides and loft..." : L"Making the sketches...");
    CWaitCursor wait;
    SeriesReport report;
    std::wstring message;
    const bool ok = CreateSeriesSketches(m_sessionId, *pScan, report, message);
    CString status;
    if (report.sketches > 0)
    {
        status.Format(L"Scan Series %d: %zu sketch%s (%s to %s), %zu spline%s, %zu lines, %zu arcs, %zu circles, "
                      L"%zu guide curve%s, in %.1f s.",
                      report.series, report.sketches, report.sketches == 1 ? L"" : L"es", report.firstSketch.c_str(),
                      report.lastSketch.c_str(), report.splines, Plural(report.splines), report.lines, report.arcs,
                      report.circles, report.guides, Plural(report.guides), report.seconds);
        if (pScan->series.options.loft && pScan->series.plane.createFitted)
        {
            CString lofts;
            if (report.loftsRefused == 0 && report.lofts == 0)
                lofts = L" No loft: no run has two slices.";
            else
                lofts.Format(L" %zu loft%s made%s.", report.lofts, Plural(report.lofts),
                             report.loftsWithoutGuides > 0 ? L" (Alibre refused the guides, so without them)" : L"");
            if (report.loftsRefused > 0)
            {
                CString refused;
                refused.Format(L" Alibre refused %zu loft%s: %s.", report.loftsRefused, Plural(report.loftsRefused),
                               report.loftError.c_str());
                lofts += refused;
            }
            status += lofts;
        }
        else if (pScan->series.options.loft)
        {
            status += L" No loft: tick Create Fitted Sketch to loft through the fitted splines.";
        }
        if (report.splinesRefused + report.guidesRefused + report.constraintsRefused + report.dimensionsRefused > 0)
        {
            CString refused;
            refused.Format(L" Alibre refused %zu splines, %zu guides, %zu constraints and %zu dimensions (see the log).",
                           report.splinesRefused, report.guidesRefused, report.constraintsRefused,
                           report.dimensionsRefused);
            status += refused;
        }
    }
    if (!ok)
    {
        status += (status.IsEmpty() ? L"" : L" ");
        status += message.c_str();
        MessageBeep(MB_ICONWARNING);
    }
    SetStatus(status);
    ClearAlibreSelection();
    RefreshScanViews();
}
