// OrientDialog.cpp : Orient: move, turn, scale, fit planes and align the scan.

#include "stdafx.h"
#include "ScanToAlibre.h"
#include "OrientDialog.h"
#include "ScanDocument.h"
#include "ScanImport.h"
#include "ScanViewCommand.h"
#include "Log.h"

#include "Orient.h"

#include <cmath>
#include <cwchar>

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

using scancore::Feature;
using scancore::Point3;
using scancore::Transform;

namespace
{
    const double kPi = 3.14159265358979323846;

    // What the dialog shows when it opens again, for this Alibre session.
    struct OrientChoices
    {
        double move[3] = { 0, 0, 0 };
        int rotateAxis = 2;         // Z
        double angle = 90;
        int rotatePivot = 1;        // the scan's centre
        double scale = 1;
        int scalePivot = 1;
        int axisToPlane = 0;        // square to it
    };
    OrientChoices g_choices;

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

    CString StepText(const scancore::AlignState& state)
    {
        if (state.Done())
            return L"Fully aligned. Start Over to align again.";
        CString text;
        text.Format(L"Step %d: a plane onto a plane, an axis onto an axis or a plane, or a point onto a point. "
                    L"Still free: %s.",
                    state.step + 1, static_cast<LPCWSTR>(Wide(scancore::FreedomText(state))));
        return text;
    }
}

void OrientViewChanged()
{
    if (COrientDlg* pDialog = dynamic_cast<COrientDlg*>(OpenStepDialog()))
        pDialog->UpdateFromScan();
}

BEGIN_MESSAGE_MAP(COrientDlg, CStepDialog)
    ON_WM_DESTROY()
    ON_BN_CLICKED(IDC_OR_MOVE, &COrientDlg::OnMove)
    ON_BN_CLICKED(IDC_OR_CENTRE, &COrientDlg::OnCentre)
    ON_BN_CLICKED(IDC_OR_ROTATE, &COrientDlg::OnRotate)
    ON_BN_CLICKED(IDC_OR_SCALE_APPLY, &COrientDlg::OnScale)
    ON_BN_CLICKED(IDC_OR_PICK_SCAN, &COrientDlg::OnPickScan)
    ON_BN_CLICKED(IDC_OR_PICK_TARGET, &COrientDlg::OnPickTarget)
    ON_BN_CLICKED(IDC_OR_SELECT_AREA, &COrientDlg::OnSelectArea)
    ON_EN_CHANGE(IDC_OR_GROW_ANGLE, &COrientDlg::OnGrowChanged)
    ON_EN_CHANGE(IDC_OR_GROW_DIST, &COrientDlg::OnGrowChanged)
    ON_BN_CLICKED(IDC_OR_CLEAR, &COrientDlg::OnClearSelection)
    ON_BN_CLICKED(IDC_OR_FIT, &COrientDlg::OnFitPlane)
    ON_BN_CLICKED(IDC_OR_FIT_AXIS, &COrientDlg::OnFitAxis)
    ON_BN_CLICKED(IDC_OR_SELECT_CYLINDER, &COrientDlg::OnSelectCylinder)
    ON_BN_CLICKED(IDC_OR_ALIGN, &COrientDlg::OnAlign)
    ON_BN_CLICKED(IDC_OR_START_OVER, &COrientDlg::OnStartOver)
    ON_BN_CLICKED(IDC_OR_UNDO, &COrientDlg::OnUndo)
END_MESSAGE_MAP()

COrientDlg::COrientDlg(const std::wstring& sessionId)
    : CStepDialog(IDD_ORIENT, sessionId)
{
}

BOOL COrientDlg::OnInitDialog()
{
    CStepDialog::OnInitDialog();

    m_rotateAxis.SubclassDlgItem(IDC_OR_ROT_AXIS, this);
    m_rotatePivot.SubclassDlgItem(IDC_OR_ROT_PIVOT, this);
    m_scalePivot.SubclassDlgItem(IDC_OR_SCALE_PIVOT, this);
    m_axisToPlane.SubclassDlgItem(IDC_OR_AXIS_PLANE, this);
    m_axisToPlane.AddString(L"square to it");
    m_axisToPlane.AddString(L"lying in it");
    for (const wchar_t* axis : { L"X", L"Y", L"Z" })
        m_rotateAxis.AddString(axis);
    for (CComboBox* pPivot : { &m_rotatePivot, &m_scalePivot })
    {
        pPivot->AddString(L"the origin");
        pPivot->AddString(L"the scan's centre");
    }

    const OrientChoices& c = g_choices;
    SetDlgItemText(IDC_OR_MOVE_X, Number(c.move[0]));
    SetDlgItemText(IDC_OR_MOVE_Y, Number(c.move[1]));
    SetDlgItemText(IDC_OR_MOVE_Z, Number(c.move[2]));
    m_rotateAxis.SetCurSel(c.rotateAxis);
    SetDlgItemText(IDC_OR_ROT_ANGLE, Number(c.angle));
    m_rotatePivot.SetCurSel(c.rotatePivot);
    SetDlgItemText(IDC_OR_SCALE, Number(c.scale));
    m_scalePivot.SetCurSel(c.scalePivot);
    m_axisToPlane.SetCurSel(c.axisToPlane);

    if (ScanDocument* pScan = FindScan(m_sessionId))
    {
        pScan->orienting = true;
        pScan->chosen = -1;
        pScan->haveTarget = false;
        pScan->clickMode = ScanDocument::ClickMode::None;
        SetDlgItemText(IDC_OR_GROW_ANGLE, Number(pScan->growAngle));
        SetDlgItemText(IDC_OR_GROW_DIST, Number(pScan->growDistance));
    }
    RefreshScanViews();
    UpdateFromScan();
    ShowClickMode();
    SetStatus(L"");
    return TRUE;
}

void COrientDlg::OnDestroy()
{
    if (ScanDocument* pScan = FindScan(m_sessionId))
    {
        pScan->orienting = false;
        pScan->chosen = -1;
        pScan->haveTarget = false;
    }
    RefreshScanViews();
    CStepDialog::OnDestroy();
}

void COrientDlg::UpdateFromScan()
{
    ScanDocument* pScan = FindScan(m_sessionId);
    CString info, selected, scanFeature, target;
    scancore::AlignState align;
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
        selected.Format(L"%zu triangles selected", pScan->selection.size());
        const std::vector<Feature> features = pScan->Features();
        scanFeature = (pScan->chosen >= 0 && pScan->chosen < static_cast<int>(features.size()))
                          ? Wide(features[pScan->chosen].name)
                          : CString(L"(none: push Scan, then click the scan's plane, axis or centre)");
        target = pScan->haveTarget ? Wide(pScan->target.name)
                                   : CString(L"(none: push To, then click Alibre's plane, axis or point)");
        align = pScan->align;
    }
    SetDlgItemText(IDC_OR_INFO, info);
    SetDlgItemText(IDC_OR_SELECTED, selected);
    SetDlgItemText(IDC_OR_SCAN_FEATURE, scanFeature);
    SetDlgItemText(IDC_OR_TARGET, target);
    SetDlgItemText(IDC_OR_STEP, StepText(align));

    const bool has = pScan != nullptr;
    for (int id : { IDC_OR_MOVE, IDC_OR_CENTRE, IDC_OR_ROTATE, IDC_OR_SCALE_APPLY, IDC_OR_START_OVER })
        GetDlgItem(id)->EnableWindow(has);
    GetDlgItem(IDC_OR_FIT)->EnableWindow(has && !pScan->selection.empty());
    GetDlgItem(IDC_OR_FIT_AXIS)->EnableWindow(has && !pScan->selection.empty());
    GetDlgItem(IDC_OR_CLEAR)->EnableWindow(has && !pScan->selection.empty());
    GetDlgItem(IDC_OR_ALIGN)->EnableWindow(has && pScan->chosen >= 0 && pScan->haveTarget && !pScan->align.Done());
    GetDlgItem(IDC_OR_UNDO)->EnableWindow(has && pScan->CanUndo());
}

void COrientDlg::SetStatus(const CString& text)
{
    SetDlgItemText(IDC_OR_STATUS, text);
    GetDlgItem(IDC_OR_STATUS)->UpdateWindow();
}

bool COrientDlg::ReadNumber(int id, double& value)
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

void COrientDlg::ScanChanged()
{
    MarkPartChanged(m_sessionId, L"Scan oriented. Changed by ScanToAlibre so the part saves its scan.");
    RefreshScanViews();
    UpdateFromScan();
}

void COrientDlg::MoveScan(const Transform& transform, const CString& status)
{
    ScanDocument* pScan = FindScan(m_sessionId);
    if (pScan == nullptr)
        return;
    CWaitCursor wait;
    const bool wasAligning = pScan->align.step > 0;
    pScan->Move(transform, scancore::AlignState());
    Log(L"[orient] %s", static_cast<LPCWSTR>(status));
    ScanChanged();
    SetStatus(status + (wasAligning ? L" The alignment starts over at step 1." : L""));
}

void COrientDlg::OnMove()
{
    AFX_MANAGE_STATE(AfxGetStaticModuleState());
    OrientChoices& c = g_choices;
    if (!ReadNumber(IDC_OR_MOVE_X, c.move[0]) || !ReadNumber(IDC_OR_MOVE_Y, c.move[1]) ||
        !ReadNumber(IDC_OR_MOVE_Z, c.move[2]))
        return;
    CString status;
    status.Format(L"Moved by %g, %g, %g mm.", c.move[0], c.move[1], c.move[2]);
    MoveScan(Transform::Translation({ c.move[0], c.move[1], c.move[2] }), status);
}

void COrientDlg::OnCentre()
{
    AFX_MANAGE_STATE(AfxGetStaticModuleState());
    ScanDocument* pScan = FindScan(m_sessionId);
    if (pScan == nullptr)
        return;
    const Point3 centre = pScan->box.centre;
    CString status;
    status.Format(L"Moved the fitted box's centre from %.3f, %.3f, %.3f to the origin.", centre.x, centre.y,
                  centre.z);
    MoveScan(Transform::Translation(centre * -1.0), status);
}

void COrientDlg::OnRotate()
{
    AFX_MANAGE_STATE(AfxGetStaticModuleState());
    ScanDocument* pScan = FindScan(m_sessionId);
    if (pScan == nullptr)
        return;
    OrientChoices& c = g_choices;
    if (!ReadNumber(IDC_OR_ROT_ANGLE, c.angle))
        return;
    c.rotateAxis = m_rotateAxis.GetCurSel();
    c.rotatePivot = m_rotatePivot.GetCurSel();
    const Point3 axes[3] = { { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } };
    const Point3 pivot = (c.rotatePivot == 1) ? pScan->box.centre : Point3{};
    CString status;
    status.Format(L"Turned %g degrees about %c through %s.", c.angle, L"XYZ"[c.rotateAxis],
                  c.rotatePivot == 1 ? L"the scan's centre" : L"the origin");
    MoveScan(Transform::Rotation(axes[c.rotateAxis], c.angle * kPi / 180.0, pivot), status);
}

void COrientDlg::OnScale()
{
    AFX_MANAGE_STATE(AfxGetStaticModuleState());
    ScanDocument* pScan = FindScan(m_sessionId);
    if (pScan == nullptr)
        return;
    OrientChoices& c = g_choices;
    if (!ReadNumber(IDC_OR_SCALE, c.scale))
        return;
    if (c.scale <= 0.0 || c.scale == 1.0)
    {
        SetStatus(L"The factor must be more than 0, and not 1.");
        MessageBeep(MB_ICONWARNING);
        return;
    }
    c.scalePivot = m_scalePivot.GetCurSel();
    const Point3 pivot = (c.scalePivot == 1) ? pScan->box.centre : Point3{};
    CString status;
    status.Format(L"Scaled by %g through %s.", c.scale, c.scalePivot == 1 ? L"the scan's centre" : L"the origin");
    MoveScan(Transform::Scaling(c.scale, pivot), status);
}

void COrientDlg::OnPickScan()
{
    ToggleClickMode(static_cast<int>(ScanDocument::ClickMode::Scan));
}

void COrientDlg::OnPickTarget()
{
    ToggleClickMode(static_cast<int>(ScanDocument::ClickMode::Target));
}

void COrientDlg::OnSelectArea()
{
    ToggleClickMode(static_cast<int>(ScanDocument::ClickMode::Area));
}

void COrientDlg::OnSelectCylinder()
{
    ToggleClickMode(static_cast<int>(ScanDocument::ClickMode::Cylinder));
}

void COrientDlg::ToggleClickMode(int mode)
{
    ScanDocument* pScan = FindScan(m_sessionId);
    if (pScan == nullptr)
        return;
    using Mode = ScanDocument::ClickMode;
    const Mode pushed = static_cast<Mode>(mode);
    pScan->clickMode = (pScan->clickMode == pushed) ? Mode::None : pushed;
    ShowClickMode();
    switch (pScan->clickMode)
    {
    case Mode::Scan:
        SetStatus(L"Click one of the scan's planes, axes or its centre.");
        break;
    case Mode::Target:
        SetStatus(L"Click one of Alibre's planes, axes or points (show them in the Design Explorer if they're "
                  L"hidden).");
        break;
    case Mode::Area:
        SetStatus(L"Click a flat area of the scan to select it; click a selected area to take it away.");
        break;
    case Mode::Cylinder:
        SetStatus(L"Click a cylinder of the scan, outside or a hole, to select it; click a selected one to take it "
                  L"away.");
        break;
    default:
        SetStatus(L"Clicks in the view are Alibre's.");
        break;
    }
}

void COrientDlg::ShowClickMode()
{
    const ScanDocument* pScan = FindScan(m_sessionId);
    const ScanDocument::ClickMode mode = (pScan != nullptr) ? pScan->clickMode : ScanDocument::ClickMode::None;
    CheckDlgButton(IDC_OR_PICK_SCAN, mode == ScanDocument::ClickMode::Scan ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(IDC_OR_PICK_TARGET, mode == ScanDocument::ClickMode::Target ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(IDC_OR_SELECT_AREA, mode == ScanDocument::ClickMode::Area ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(IDC_OR_SELECT_CYLINDER, mode == ScanDocument::ClickMode::Cylinder ? BST_CHECKED : BST_UNCHECKED);
}

void COrientDlg::OnGrowChanged()
{
    ScanDocument* pScan = FindScan(m_sessionId);
    if (pScan == nullptr)
        return;
    // Kept only while they read as numbers in range; otherwise the last good ones stay.
    for (int id : { IDC_OR_GROW_ANGLE, IDC_OR_GROW_DIST })
    {
        CString text;
        GetDlgItemText(id, text);
        wchar_t* end = nullptr;
        const double value = wcstod(text, &end);
        if (text.IsEmpty() || end == nullptr || *end != L'\0' || !(value > 0.0))
            continue;
        if (id == IDC_OR_GROW_ANGLE && value <= 90.0)
            pScan->growAngle = value;
        else if (id == IDC_OR_GROW_DIST)
            pScan->growDistance = value;
    }
}

void COrientDlg::OnClearSelection()
{
    ScanDocument* pScan = FindScan(m_sessionId);
    if (pScan == nullptr)
        return;
    pScan->SetSelection({});
    RefreshScanViews();
    UpdateFromScan();
    SetStatus(L"Selection cleared.");
}

void COrientDlg::OnFitPlane()
{
    AFX_MANAGE_STATE(AfxGetStaticModuleState());
    ScanDocument* pScan = FindScan(m_sessionId);
    if (pScan == nullptr || pScan->selection.empty())
        return;

    Feature plane;
    double rms = 0.0;
    if (!scancore::FitPlane(pScan->mesh, pScan->selection, plane, &rms))
    {
        SetStatus(L"A plane couldn't be fitted to that selection.");
        return;
    }
    int planes = 0;
    for (const Feature& f : pScan->fitted)
        planes += (f.kind == Feature::Kind::Plane) ? 1 : 0;
    plane.name = "Plane " + std::to_string(planes + 1);
    const size_t triangles = pScan->selection.size();
    Log(L"[orient] %S fitted to %zu triangles: point (%.3f, %.3f, %.3f), normal (%.4f, %.4f, %.4f), rms %.4f mm",
        plane.name.c_str(), triangles, plane.point.x, plane.point.y, plane.point.z, plane.direction.x,
        plane.direction.y, plane.direction.z, rms);
    CString status;
    status.Format(L"%s fitted to %zu triangles; they're %.3f mm from it on average (rms). It's chosen to align.",
                  static_cast<LPCWSTR>(Wide(plane.name)), triangles, rms);
    AddFitted(plane, status);
}

void COrientDlg::OnFitAxis()
{
    AFX_MANAGE_STATE(AfxGetStaticModuleState());
    ScanDocument* pScan = FindScan(m_sessionId);
    if (pScan == nullptr || pScan->selection.empty())
        return;

    scancore::CylinderFit fit;
    if (!scancore::FitCylinder(pScan->mesh, pScan->selection, fit))
    {
        SetStatus(L"An axis couldn't be fitted: select more of the cylinder, so its surface turns at least about "
                  L"25 degrees.");
        MessageBeep(MB_ICONWARNING);
        return;
    }
    int axes = 0;
    for (const Feature& f : pScan->fitted)
        axes += (f.kind == Feature::Kind::Axis) ? 1 : 0;
    char name[96];
    sprintf_s(name, "Axis %d (diameter %.3f, %s)", axes + 1, 2.0 * fit.radius, fit.hole ? "hole" : "outside");
    fit.axis.name = name;
    const size_t triangles = pScan->selection.size();
    Log(L"[orient] %S fitted to %zu triangles: point (%.3f, %.3f, %.3f), direction (%.4f, %.4f, %.4f), radius %.4f, "
        L"rms %.4f mm",
        fit.axis.name.c_str(), triangles, fit.axis.point.x, fit.axis.point.y, fit.axis.point.z, fit.axis.direction.x,
        fit.axis.direction.y, fit.axis.direction.z, fit.radius, fit.rms);
    CString status;
    status.Format(L"%s fitted to %zu triangles; they're %.3f mm from the cylinder on average (rms). It's chosen to "
                  L"align.",
                  static_cast<LPCWSTR>(Wide(fit.axis.name)), triangles, fit.rms);
    AddFitted(fit.axis, status);
}

void COrientDlg::AddFitted(const Feature& feature, const CString& status)
{
    ScanDocument* pScan = FindScan(m_sessionId);
    if (pScan == nullptr)
        return;
    pScan->AddFitted(feature);
    MarkPartChanged(m_sessionId, L"Plane or axis fitted. Changed by ScanToAlibre so the part saves it.");
    pScan->SetSelection({});
    pScan->chosen = static_cast<int>(pScan->Features().size()) - 1;
    pScan->clickMode = ScanDocument::ClickMode::None;
    ShowClickMode();
    RefreshScanViews();
    UpdateFromScan();
    SetStatus(status);
}

void COrientDlg::OnAlign()
{
    AFX_MANAGE_STATE(AfxGetStaticModuleState());
    ScanDocument* pScan = FindScan(m_sessionId);
    if (pScan == nullptr || pScan->chosen < 0 || !pScan->haveTarget)
        return;
    const std::vector<Feature> features = pScan->Features();
    if (pScan->chosen >= static_cast<int>(features.size()))
        return;
    const Feature& scanFeature = features[pScan->chosen];

    scancore::AlignState state = pScan->align;
    Transform move;
    std::string message;
    const bool flip = IsDlgButtonChecked(IDC_OR_FLIP) == BST_CHECKED;
    g_choices.axisToPlane = m_axisToPlane.GetCurSel();
    const scancore::AxisToPlane axisToPlane =
        (g_choices.axisToPlane == 1) ? scancore::AxisToPlane::InPlane : scancore::AxisToPlane::Square;
    if (!scancore::AlignStep(scanFeature, pScan->target, flip, axisToPlane, state, move, message))
    {
        SetStatus(Wide(message));
        MessageBeep(MB_ICONWARNING);
        return;
    }

    CWaitCursor wait;
    pScan->Move(move, state);
    Log(L"[orient] step %d: %S onto %S%s", state.step, scanFeature.name.c_str(), pScan->target.name.c_str(),
        flip ? L", flipped" : L"");
    CString status;
    status.Format(L"Step %d: %s lined up with %s.%s%s", state.step, static_cast<LPCWSTR>(Wide(scanFeature.name)),
                  static_cast<LPCWSTR>(Wide(pScan->target.name)), message.empty() ? L"" : L" ",
                  static_cast<LPCWSTR>(Wide(message)));
    pScan->chosen = -1;
    pScan->haveTarget = false;
    CheckDlgButton(IDC_OR_FLIP, BST_UNCHECKED);
    ScanChanged();
    SetStatus(status);
}

void COrientDlg::OnStartOver()
{
    ScanDocument* pScan = FindScan(m_sessionId);
    if (pScan == nullptr)
        return;
    pScan->align = scancore::AlignState();
    UpdateFromScan();
    SetStatus(L"The alignment starts again at step 1.");
}

void COrientDlg::OnUndo()
{
    AFX_MANAGE_STATE(AfxGetStaticModuleState());
    ScanDocument* pScan = FindScan(m_sessionId);
    if (pScan == nullptr || !pScan->CanUndo())
        return;
    CWaitCursor wait;
    pScan->Undo();
    Log(L"[orient] undo");
    ScanChanged();
    SetStatus(L"Undone.");
}
