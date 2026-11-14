// SectionDialog.cpp : 4 Section: slice the scan, fit lines and arcs, make a sketch.

#include "stdafx.h"
#include "ScanToAlibre.h"
#include "SectionDialog.h"
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

using scancore::ProfileConstraint;
using scancore::ProfileSegment;

namespace
{
    // The Round radii to choices.
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
}

void SectionViewChanged()
{
    if (CSectionDlg* pDialog = dynamic_cast<CSectionDlg*>(OpenStepDialog()))
        pDialog->UpdateFromScan();
    else
        SeriesViewChanged();
}

void SectionPlanePicked()
{
    if (CSectionDlg* pDialog = dynamic_cast<CSectionDlg*>(OpenStepDialog()))
        pDialog->PlanePicked();
    else
        SeriesPlanePicked();
}

void CSectionDlg::ClearAlibreSelection()
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
        Log(L"[section] clearing Alibre's selection failed: hr=0x%08X", static_cast<unsigned>(error.Error()));
    }
}

void CSectionDlg::PlanePicked()
{
    ShowClickMode();
    UpdateFromScan();
    SetStatus(L"Plane chosen. Create Sketch, or press a Pick button for another plane.");
}

BEGIN_MESSAGE_MAP(CSectionDlg, CStepDialog)
    ON_WM_DESTROY()
    ON_BN_CLICKED(IDC_SEC_PICK_ALIBRE, &CSectionDlg::OnPickAlibre)
    ON_BN_CLICKED(IDC_SEC_PICK_SCAN, &CSectionDlg::OnPickScan)
    ON_EN_CHANGE(IDC_SEC_OFFSET, &CSectionDlg::OnOffsetChanged)
    ON_EN_CHANGE(IDC_SEC_TOLERANCE, &CSectionDlg::OnToleranceChanged)
    ON_EN_CHANGE(IDC_SIL_SMALLEST_HOLE, &CSectionDlg::OnSmallestHoleChanged)
    ON_BN_CLICKED(IDC_SEC_HV, &CSectionDlg::OnIntentChanged)
    ON_BN_CLICKED(IDC_SEC_TANGENT, &CSectionDlg::OnIntentChanged)
    ON_BN_CLICKED(IDC_SEC_CONCENTRIC, &CSectionDlg::OnIntentChanged)
    ON_BN_CLICKED(IDC_SEC_SYMMETRIC, &CSectionDlg::OnIntentChanged)
    ON_BN_CLICKED(IDC_SEC_CREATE_RAW, &CSectionDlg::OnSketchOptionsChanged)
    ON_BN_CLICKED(IDC_SEC_CREATE_FITTED, &CSectionDlg::OnSketchOptionsChanged)
    ON_BN_CLICKED(IDC_SEC_FIT_ARCS, &CSectionDlg::OnSketchOptionsChanged)
    ON_CBN_SELCHANGE(IDC_SEC_ROUND, &CSectionDlg::OnIntentChanged)
    ON_BN_CLICKED(IDC_SEC_CREATE, &CSectionDlg::OnCreate)
END_MESSAGE_MAP()

CSectionDlg::CSectionDlg(const std::wstring& sessionId, bool silhouette)
    : CStepDialog(silhouette ? IDD_SILHOUETTE : IDD_SECTION, sessionId), m_silhouette(silhouette)
{
}

BOOL CSectionDlg::OnInitDialog()
{
    CStepDialog::OnInitDialog();

    m_round.SubclassDlgItem(IDC_SEC_ROUND, this);
    m_round.AddString(L"0.1 mm");
    m_round.AddString(L"0.01 mm");
    m_round.AddString(L"Off");

    m_filling = true;
    if (ScanDocument* pScan = FindScan(m_sessionId))
    {
        pScan->sectioning = !m_silhouette;
        pScan->silhouetting = m_silhouette;
        pScan->clickMode = ScanDocument::ClickMode::None;
        const ScanDocument::Section& s = Sec(*pScan);
        SetDlgItemText(IDC_SEC_OFFSET, Number(s.offset));
        SetDlgItemText(IDC_SEC_TOLERANCE, Number(s.tolerance));
        if (m_silhouette)
            SetDlgItemText(IDC_SIL_SMALLEST_HOLE, Number(s.smallestHole));
        CheckDlgButton(IDC_SEC_HV, s.snap.horizontalVertical ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(IDC_SEC_TANGENT, s.snap.tangent ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(IDC_SEC_CONCENTRIC, s.snap.concentric ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(IDC_SEC_SYMMETRIC, s.snap.symmetric ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(IDC_SEC_CREATE_RAW, s.createRaw ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(IDC_SEC_CREATE_FITTED, s.createFitted ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(IDC_SEC_FIT_ARCS, s.snap.fitArcs ? BST_CHECKED : BST_UNCHECKED);
        int round = 2;
        for (int k = 0; k < 3; ++k)
            if (std::fabs(s.snap.roundTo - kRoundSteps[k]) < 1e-12)
                round = k;
        m_round.SetCurSel(round);
        // The scan may have moved or changed since the last section.
        CWaitCursor wait;
        pScan->UpdateSection();
    }
    m_filling = false;
    RefreshScanViews();
    UpdateFromScan();
    ShowClickMode();
    SetStatus(m_silhouette ? L"Push Alibre Plane or Scan Plane, then click the plane to see the scan's outline on."
                           : L"Push Alibre Plane or Scan Plane, then click the plane to slice the scan with.");
    return TRUE;
}

void CSectionDlg::OnDestroy()
{
    if (ScanDocument* pScan = FindScan(m_sessionId))
    {
        pScan->sectioning = false;
        pScan->silhouetting = false;
        pScan->clickMode = ScanDocument::ClickMode::None;
        pScan->chosen = -1;
    }
    RefreshScanViews();
    CStepDialog::OnDestroy();
}

void CSectionDlg::UpdateFromScan()
{
    ScanDocument* pScan = FindScan(m_sessionId);
    CString info, plane, result, depth;
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
        const ScanDocument::Section& s = Sec(*pScan);
        if (!s.havePlane)
        {
            plane = L"(none yet)";
        }
        else
        {
            plane.Format(L"%s%s", static_cast<LPCWSTR>(Wide(s.planeName)),
                         s.alibrePlane != nullptr ? (s.alibreFace ? L" (Alibre face)" : L" (Alibre plane)")
                                                  : L" (scan plane; a design plane is made for the sketch)");
            const scancore::SectionResult& r = s.result;
            size_t loops = 0, runs = 0, lines = 0, arcs = 0, circles = 0;
            for (size_t k = 0; k < r.profiles.size(); ++k)
            {
                (r.profiles[k].closed ? loops : runs)++;
                for (const ProfileSegment& seg : r.profiles[k].segments)
                    (seg.kind == ProfileSegment::Kind::Line ? lines
                                                            : seg.kind == ProfileSegment::Kind::Arc ? arcs : circles)++;
            }
            size_t counts[6] = {};
            for (const ProfileConstraint& c : s.constraints)
                counts[static_cast<int>(c.kind)]++;
            CString shapes;
            if (m_silhouette)
            {
                const scancore::SilhouetteInfo& i = s.silhouetteInfo;
                shapes.Format(L"%zu piece%s and %zu hole%s", i.pieces, i.pieces == 1 ? L"" : L"s", i.holes,
                              i.holes == 1 ? L"" : L"s");
                if (i.filledHoles + i.droppedPieces > 0)
                {
                    CString smaller;
                    smaller.Format(L" (%zu smaller hole%s filled, %zu speck%s left out)", i.filledHoles,
                                   i.filledHoles == 1 ? L"" : L"s", i.droppedPieces, i.droppedPieces == 1 ? L"" : L"s");
                    shapes += smaller;
                }
            }
            else
            {
                shapes.Format(L"%zu closed and %zu open", loops, runs);
            }
            if (r.profiles.empty())
                result = m_silhouette ? L"There's no outline to see. Choose another plane."
                                      : L"The plane doesn't cut the scan. Change the offset or choose another plane.";
            else
                result.Format(L"%s (%zu points): %zu lines, %zu arcs, %zu circles, within %.3f mm of the %s. %zu "
                              L"horizontal, %zu vertical, %zu tangent, %zu concentric (horizontal and vertical are "
                              L"judged again in the sketch's own axes).",
                              static_cast<LPCWSTR>(shapes), r.slicePoints, lines, arcs, circles, r.largestDeviation,
                              m_silhouette ? L"outline" : L"slice",
                              counts[static_cast<int>(ProfileConstraint::Kind::Horizontal)],
                              counts[static_cast<int>(ProfileConstraint::Kind::Vertical)],
                              counts[static_cast<int>(ProfileConstraint::Kind::Tangent)],
                              counts[static_cast<int>(ProfileConstraint::Kind::Concentric)]);
            if (s.snap.symmetric && !r.profiles.empty())
            {
                CString symmetric;
                if (s.symmetryName.empty())
                    symmetric = L" Symmetric: no symmetry plane is square to this plane (fit one in Orient with Find "
                                L"Symmetry or Mid Plane, or Detect in Surface).";
                else
                    symmetric.Format(L" Symmetric about %s: %zu pairs of points; the scan's two sides differ by up "
                                     L"to %.3f mm, and the fit goes halfway.",
                                     static_cast<LPCWSTR>(Wide(s.symmetryName)),
                                     counts[static_cast<int>(ProfileConstraint::Kind::Symmetric)],
                                     r.symmetricGap);
                result += symmetric;
            }
            depth.Format(L"The scan reaches %.2f mm in front of the plane and %.2f mm behind it.", r.ahead,
                         -r.behind);
            canCreate = !r.profiles.empty();
        }
    }
    SetDlgItemText(IDC_SEC_INFO, info);
    SetDlgItemText(IDC_SEC_PLANE, plane);
    SetDlgItemText(IDC_SEC_RESULT, result);
    SetDlgItemText(IDC_SEC_DEPTH, depth);
    if (pScan != nullptr && !Sec(*pScan).createRaw && !Sec(*pScan).createFitted)
        canCreate = false;
    GetDlgItem(IDC_SEC_CREATE)->EnableWindow(canCreate);
    GetDlgItem(IDC_SEC_PICK_ALIBRE)->EnableWindow(pScan != nullptr);
    GetDlgItem(IDC_SEC_PICK_SCAN)->EnableWindow(pScan != nullptr);
}

void CSectionDlg::SetStatus(const CString& text)
{
    SetDlgItemText(IDC_SEC_STATUS, text);
    GetDlgItem(IDC_SEC_STATUS)->UpdateWindow();
}

void CSectionDlg::OnPickAlibre()
{
    ToggleClickMode(static_cast<int>(ScanDocument::ClickMode::Target));
}

void CSectionDlg::OnPickScan()
{
    ToggleClickMode(static_cast<int>(ScanDocument::ClickMode::Scan));
}

void CSectionDlg::ToggleClickMode(int mode)
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
        SetStatus(L"Click one of Alibre's design planes or a flat face of the part (show the planes in the Design "
                  L"Explorer if they're hidden).");
        break;
    case Mode::Scan:
        SetStatus(L"Click one of the scan's planes: its box's centre planes or a plane fitted in Orient.");
        break;
    default:
        SetStatus(L"Clicks in the view are Alibre's.");
        break;
    }
}

void CSectionDlg::ShowClickMode()
{
    const ScanDocument* pScan = FindScan(m_sessionId);
    const ScanDocument::ClickMode mode = (pScan != nullptr) ? pScan->clickMode : ScanDocument::ClickMode::None;
    CheckDlgButton(IDC_SEC_PICK_ALIBRE, mode == ScanDocument::ClickMode::Target ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(IDC_SEC_PICK_SCAN, mode == ScanDocument::ClickMode::Scan ? BST_CHECKED : BST_UNCHECKED);
}

void CSectionDlg::Reslice()
{
    ScanDocument* pScan = FindScan(m_sessionId);
    if (pScan == nullptr)
        return;
    CWaitCursor wait;
    pScan->UpdateSection();
    RefreshScanViews();
    UpdateFromScan();
}

void CSectionDlg::OnOffsetChanged()
{
    ScanDocument* pScan = FindScan(m_sessionId);
    double value = 0.0;
    if (m_filling || pScan == nullptr || !ReadEdit(this, IDC_SEC_OFFSET, value))
        return;
    if (value == Sec(*pScan).offset)
        return;
    Sec(*pScan).offset = value;
    Reslice();
}

void CSectionDlg::OnToleranceChanged()
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
    if (value == Sec(*pScan).tolerance)
        return;
    Sec(*pScan).tolerance = value;
    // Arcs whose centres are as close as the tolerance are concentric.
    Sec(*pScan).snap.concentricDistance = value;
    Sec(*pScan).snap.symmetryDistance = value;
    SetStatus(L"");
    Reslice();
}

void CSectionDlg::OnSmallestHoleChanged()
{
    ScanDocument* pScan = FindScan(m_sessionId);
    double value = 0.0;
    if (m_filling || pScan == nullptr || !m_silhouette || !ReadEdit(this, IDC_SIL_SMALLEST_HOLE, value))
        return;
    if (value < 0.0 || value > 1000.0)
    {
        SetStatus(L"The smallest hole must be from 0 to 1000 mm.");
        return;
    }
    if (value == Sec(*pScan).smallestHole)
        return;
    Sec(*pScan).smallestHole = value;
    SetStatus(L"");
    Reslice();
}

void CSectionDlg::OnSketchOptionsChanged()
{
    ScanDocument* pScan = FindScan(m_sessionId);
    if (m_filling || pScan == nullptr)
        return;
    ScanDocument::Section& s = Sec(*pScan);
    s.createRaw = IsDlgButtonChecked(IDC_SEC_CREATE_RAW) == BST_CHECKED;
    s.createFitted = IsDlgButtonChecked(IDC_SEC_CREATE_FITTED) == BST_CHECKED;
    const bool arcs = IsDlgButtonChecked(IDC_SEC_FIT_ARCS) == BST_CHECKED;
    if (arcs != s.snap.fitArcs)
    {
        s.snap.fitArcs = arcs;
        Reslice();      // fitted again, with or without arcs
    }
    else
    {
        UpdateFromScan();
    }
    if (!s.createRaw && !s.createFitted)
        SetStatus(L"Tick Create Raw Sketch or Create Fitted Sketch to make a sketch.");
    else
        SetStatus(L"");
}

void CSectionDlg::OnIntentChanged()
{
    ScanDocument* pScan = FindScan(m_sessionId);
    if (m_filling || pScan == nullptr)
        return;
    scancore::SnapOptions& snap = Sec(*pScan).snap;
    snap.horizontalVertical = IsDlgButtonChecked(IDC_SEC_HV) == BST_CHECKED;
    snap.tangent = IsDlgButtonChecked(IDC_SEC_TANGENT) == BST_CHECKED;
    snap.concentric = IsDlgButtonChecked(IDC_SEC_CONCENTRIC) == BST_CHECKED;
    snap.symmetric = IsDlgButtonChecked(IDC_SEC_SYMMETRIC) == BST_CHECKED;
    const int round = m_round.GetCurSel();
    snap.roundTo = (round >= 0 && round < 3) ? kRoundSteps[round] : 0.0;
    // Symmetric moves the fitted segments, so fit again from the slice.
    Reslice();
}

void CSectionDlg::OnCreate()
{
    AFX_MANAGE_STATE(AfxGetStaticModuleState());
    ScanDocument* pScan = FindScan(m_sessionId);
    if (pScan == nullptr || !Sec(*pScan).havePlane)
        return;
    // Picking ends, so the next press of a Pick button starts a new pick.
    pScan->clickMode = ScanDocument::ClickMode::None;
    ShowClickMode();
    SetStatus(L"Making the sketch...");
    CWaitCursor wait;
    SketchReport report;
    std::wstring message;
    if (!CreateSectionSketch(m_sessionId, *pScan, report, message))
    {
        SetStatus(message.c_str());
        MessageBeep(MB_ICONWARNING);
        return;
    }
    CString status;
    const wchar_t* what = m_silhouette ? L"outline" : L"slice";
    if (report.lines + report.arcs + report.circles == 0)
        status.Format(L"%s made on %s: the raw %s as %zu lines, in %.1f s.", report.sketchName.c_str(),
                      report.planeName.c_str(), what, report.rawLines, report.seconds);
    else
        status.Format(L"%s made on %s: %zu lines, %zu arcs, %zu circles, %zu constraints, %zu dimensions, and %zu "
                      L"construction lines along the %s, in %.1f s.",
                      report.sketchName.c_str(), report.planeName.c_str(), report.lines, report.arcs, report.circles,
                      report.constraints, report.dimensions, report.reference, what, report.seconds);
    if (report.constraintsRefused + report.dimensionsRefused > 0)
    {
        CString refused;
        refused.Format(L" Alibre refused %zu constraints and %zu dimensions (see the log).",
                       report.constraintsRefused, report.dimensionsRefused);
        status += refused;
    }
    SetStatus(status);
    // Frank: the plane (and the new sketch Alibre selects) deselected, so the
    // next pick starts clean.
    ClearAlibreSelection();
    RefreshScanViews();
}
