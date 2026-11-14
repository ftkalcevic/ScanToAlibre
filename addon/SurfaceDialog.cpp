// SurfaceDialog.cpp : 5 Surface: fit a B-spline surface to selected areas of the
// scan and insert it into the part.

#include "stdafx.h"
#include "ScanToAlibre.h"
#include "SurfaceDialog.h"
#include "ScanDocument.h"
#include "ScanImport.h"
#include "ScanViewCommand.h"
#include "Log.h"

#include "FileIO.h"
#include "Iges.h"

#include <chrono>
#include <cmath>
#include <cwchar>

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

using scancore::SurfaceBase;
using scancore::SurfacePerimeter;

namespace
{
    CString Number(double value)
    {
        CString text;
        text.Format(L"%g", value);
        return text;
    }

    std::wstring Widen(const std::string& text)
    {
        if (text.empty())
            return std::wstring();
        const int length = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
        std::wstring wide(length, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), &wide[0], length);
        return wide;
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

    std::string Timestamp()
    {
        SYSTEMTIME now = {};
        GetLocalTime(&now);
        char text[32] = {};
        sprintf_s(text, "%04u%02u%02u.%02u%02u%02u", now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute,
                  now.wSecond);
        return text;
    }

    const int kBaseIds[4] = { IDC_SURF_BASE_AUTO, IDC_SURF_BASE_XY, IDC_SURF_BASE_YZ, IDC_SURF_BASE_ZX };
}

void SurfaceViewChanged()
{
    if (CSurfaceDlg* pDialog = dynamic_cast<CSurfaceDlg*>(OpenStepDialog()))
        pDialog->UpdateFromScan();
}

BEGIN_MESSAGE_MAP(CSurfaceDlg, CStepDialog)
    ON_WM_DESTROY()
    ON_BN_CLICKED(IDC_SURF_SMOOTH, &CSurfaceDlg::OnPickSmooth)
    ON_BN_CLICKED(IDC_SURF_BRUSH, &CSurfaceDlg::OnPickBrush)
    ON_BN_CLICKED(IDC_SURF_CLEAR, &CSurfaceDlg::OnClear)
    ON_EN_CHANGE(IDC_SURF_ANGLE, &CSurfaceDlg::OnSelectChanged)
    ON_EN_CHANGE(IDC_SURF_RADIUS, &CSurfaceDlg::OnSelectChanged)
    ON_BN_CLICKED(IDC_SURF_BASE_AUTO, &CSurfaceDlg::OnOptionsChanged)
    ON_BN_CLICKED(IDC_SURF_BASE_XY, &CSurfaceDlg::OnOptionsChanged)
    ON_BN_CLICKED(IDC_SURF_BASE_YZ, &CSurfaceDlg::OnOptionsChanged)
    ON_BN_CLICKED(IDC_SURF_BASE_ZX, &CSurfaceDlg::OnOptionsChanged)
    ON_BN_CLICKED(IDC_SURF_SYM_XY, &CSurfaceDlg::OnOptionsChanged)
    ON_BN_CLICKED(IDC_SURF_SYM_YZ, &CSurfaceDlg::OnOptionsChanged)
    ON_BN_CLICKED(IDC_SURF_SYM_ZX, &CSurfaceDlg::OnOptionsChanged)
    ON_BN_CLICKED(IDC_SURF_PROJECTION, &CSurfaceDlg::OnOptionsChanged)
    ON_BN_CLICKED(IDC_SURF_RECTANGLE, &CSurfaceDlg::OnOptionsChanged)
    ON_BN_CLICKED(IDC_SURF_BOX, &CSurfaceDlg::OnOptionsChanged)
    ON_BN_CLICKED(IDC_SURF_SELECTION, &CSurfaceDlg::OnOptionsChanged)
    ON_EN_CHANGE(IDC_SURF_OFFSET, &CSurfaceDlg::OnOptionsChanged)
    ON_EN_CHANGE(IDC_SURF_TOLERANCE, &CSurfaceDlg::OnOptionsChanged)
    ON_EN_CHANGE(IDC_SURF_SMOOTHING, &CSurfaceDlg::OnOptionsChanged)
    ON_BN_CLICKED(IDC_SURF_CREATE, &CSurfaceDlg::OnCreate)
END_MESSAGE_MAP()

CSurfaceDlg::CSurfaceDlg(const std::wstring& sessionId)
    : CStepDialog(IDD_SURFACE, sessionId)
{
}

BOOL CSurfaceDlg::OnInitDialog()
{
    CStepDialog::OnInitDialog();

    m_filling = true;
    if (ScanDocument* pScan = FindScan(m_sessionId))
    {
        pScan->surfacing = true;
        pScan->clickMode = ScanDocument::ClickMode::None;
        const ScanDocument::Surface& s = pScan->surface;
        const scancore::SurfaceFitOptions& o = s.options;
        SetDlgItemText(IDC_SURF_ANGLE, Number(s.smoothAngle));
        SetDlgItemText(IDC_SURF_RADIUS, Number(s.brushRadius));
        CheckRadioButton(IDC_SURF_BASE_AUTO, IDC_SURF_BASE_ZX, kBaseIds[static_cast<int>(o.base)]);
        CheckDlgButton(IDC_SURF_SYM_XY, o.symmetryXY ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(IDC_SURF_SYM_YZ, o.symmetryYZ ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(IDC_SURF_SYM_ZX, o.symmetryZX ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(IDC_SURF_PROJECTION, o.perimeter == SurfacePerimeter::Projection ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(IDC_SURF_RECTANGLE, o.perimeter == SurfacePerimeter::Rectangle ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(IDC_SURF_BOX, o.perimeter == SurfacePerimeter::Box ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(IDC_SURF_SELECTION, o.perimeter == SurfacePerimeter::Selection ? BST_CHECKED : BST_UNCHECKED);
        SetDlgItemText(IDC_SURF_OFFSET, Number(o.offset));
        SetDlgItemText(IDC_SURF_TOLERANCE, Number(o.tolerance));
        SetDlgItemText(IDC_SURF_SMOOTHING, Number(o.smoothing));
        // The scan may have changed since the last fit.
        CWaitCursor wait;
        pScan->UpdateSurface();
    }
    m_filling = false;
    ShowSymmetryChoices();
    RefreshScanViews();
    UpdateFromScan();
    ShowClickMode();
    SetStatus(L"Push Smooth Area or Brush, then click the scan to select the areas to fit. Click a selected area "
              L"to take it away.");
    return TRUE;
}

void CSurfaceDlg::OnDestroy()
{
    if (ScanDocument* pScan = FindScan(m_sessionId))
    {
        pScan->surfacing = false;
        pScan->clickMode = ScanDocument::ClickMode::None;
    }
    RefreshScanViews();
    CStepDialog::OnDestroy();
}

void CSurfaceDlg::UpdateFromScan()
{
    ScanDocument* pScan = FindScan(m_sessionId);
    CString info, selected, result;
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
        selected.Format(L"%zu of %zu triangles selected.", pScan->selection.size(), pScan->mesh.triangles.size());
        const scancore::SurfaceFitResult& r = pScan->surface.result;
        if (pScan->selection.empty())
        {
            result = L"Nothing selected yet.";
        }
        else if (!r.ok)
        {
            result = Widen(r.message).c_str();
        }
        else
        {
            result.Format(L"%d x %d control points. 99%% of the scan is within %.3f mm of it (largest %.3f, RMS "
                          L"%.3f), measured square to the base plane.%s",
                          r.surface.countU, r.surface.countV, r.within99, r.largest, r.rms,
                          r.outline.empty() ? L""
                          : pScan->surface.options.perimeter == SurfacePerimeter::Projection
                              ? L" Trimmed to the scan's outline."
                              : L" Trimmed to the selection's outline.");
            if (pScan->surface.options.offset > 0.0)
            {
                CString offset;
                offset.Format(L" The perimeter is %g mm bigger.", pScan->surface.options.offset);
                result += offset;
            }
            canCreate = true;
        }
    }
    SetDlgItemText(IDC_SURF_INFO, info);
    SetDlgItemText(IDC_SURF_SELECTED, selected);
    SetDlgItemText(IDC_SURF_RESULT, result);
    GetDlgItem(IDC_SURF_CREATE)->EnableWindow(canCreate);
    GetDlgItem(IDC_SURF_SMOOTH)->EnableWindow(pScan != nullptr);
    GetDlgItem(IDC_SURF_BRUSH)->EnableWindow(pScan != nullptr);
    GetDlgItem(IDC_SURF_CLEAR)->EnableWindow(pScan != nullptr && !pScan->selection.empty());
}

void CSurfaceDlg::SetStatus(const CString& text)
{
    SetDlgItemText(IDC_SURF_STATUS, text);
    GetDlgItem(IDC_SURF_STATUS)->UpdateWindow();
}

void CSurfaceDlg::OnPickSmooth()
{
    ToggleClickMode(static_cast<int>(ScanDocument::ClickMode::Smooth));
}

void CSurfaceDlg::OnPickBrush()
{
    ToggleClickMode(static_cast<int>(ScanDocument::ClickMode::Brush));
}

void CSurfaceDlg::ToggleClickMode(int mode)
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
    case Mode::Smooth:
        SetStatus(L"Click the scan to add the smooth area round the click (it stops where the surface bends by "
                  L"more than the angle). Click a selected area to take it away.");
        break;
    case Mode::Brush:
        SetStatus(L"Click the scan to add the triangles within the brush radius of the click. Click a selected "
                  L"triangle to take that much away.");
        break;
    default:
        SetStatus(L"Clicks in the view are Alibre's.");
        break;
    }
}

void CSurfaceDlg::ShowClickMode()
{
    const ScanDocument* pScan = FindScan(m_sessionId);
    const ScanDocument::ClickMode mode = (pScan != nullptr) ? pScan->clickMode : ScanDocument::ClickMode::None;
    CheckDlgButton(IDC_SURF_SMOOTH, mode == ScanDocument::ClickMode::Smooth ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(IDC_SURF_BRUSH, mode == ScanDocument::ClickMode::Brush ? BST_CHECKED : BST_UNCHECKED);
}

void CSurfaceDlg::ShowSymmetryChoices()
{
    const ScanDocument* pScan = FindScan(m_sessionId);
    const SurfaceBase base = (pScan != nullptr) ? pScan->surface.options.base : SurfaceBase::Auto;
    GetDlgItem(IDC_SURF_SYM_XY)->EnableWindow(base != SurfaceBase::XY);
    GetDlgItem(IDC_SURF_SYM_YZ)->EnableWindow(base != SurfaceBase::YZ);
    GetDlgItem(IDC_SURF_SYM_ZX)->EnableWindow(base != SurfaceBase::ZX);
}

void CSurfaceDlg::Refit()
{
    ScanDocument* pScan = FindScan(m_sessionId);
    if (pScan == nullptr)
        return;
    {
        CWaitCursor wait;
        pScan->UpdateSurface();
    }
    RefreshScanViews();
    UpdateFromScan();
}

void CSurfaceDlg::OnClear()
{
    ScanDocument* pScan = FindScan(m_sessionId);
    if (pScan == nullptr)
        return;
    pScan->SetSelection({});
    Refit();
}

void CSurfaceDlg::OnSelectChanged()
{
    ScanDocument* pScan = FindScan(m_sessionId);
    if (m_filling || pScan == nullptr)
        return;
    double angle = 0.0, radius = 0.0;
    if (ReadEdit(this, IDC_SURF_ANGLE, angle))
    {
        if (angle > 0.0 && angle < 90.0)
            pScan->surface.smoothAngle = angle;
        else
            SetStatus(L"The angle must be more than 0 and less than 90 degrees.");
    }
    if (ReadEdit(this, IDC_SURF_RADIUS, radius))
    {
        if (radius > 0.0)
            pScan->surface.brushRadius = radius;
        else
            SetStatus(L"The brush radius must be more than 0.");
    }
}

void CSurfaceDlg::OnOptionsChanged()
{
    ScanDocument* pScan = FindScan(m_sessionId);
    if (m_filling || pScan == nullptr)
        return;
    scancore::SurfaceFitOptions o = pScan->surface.options;
    for (int k = 0; k < 4; ++k)
        if (IsDlgButtonChecked(kBaseIds[k]) == BST_CHECKED)
            o.base = static_cast<SurfaceBase>(k);

    // The base plane's own symmetry box is greyed and cleared.
    m_filling = true;
    if (o.base == SurfaceBase::XY)
        CheckDlgButton(IDC_SURF_SYM_XY, BST_UNCHECKED);
    if (o.base == SurfaceBase::YZ)
        CheckDlgButton(IDC_SURF_SYM_YZ, BST_UNCHECKED);
    if (o.base == SurfaceBase::ZX)
        CheckDlgButton(IDC_SURF_SYM_ZX, BST_UNCHECKED);
    m_filling = false;
    o.symmetryXY = IsDlgButtonChecked(IDC_SURF_SYM_XY) == BST_CHECKED;
    o.symmetryYZ = IsDlgButtonChecked(IDC_SURF_SYM_YZ) == BST_CHECKED;
    o.symmetryZX = IsDlgButtonChecked(IDC_SURF_SYM_ZX) == BST_CHECKED;
    o.perimeter = IsDlgButtonChecked(IDC_SURF_RECTANGLE) == BST_CHECKED ? SurfacePerimeter::Rectangle
                  : IsDlgButtonChecked(IDC_SURF_BOX) == BST_CHECKED   ? SurfacePerimeter::Box
                  : IsDlgButtonChecked(IDC_SURF_PROJECTION) == BST_CHECKED ? SurfacePerimeter::Projection
                                                                       : SurfacePerimeter::Selection;

    double tolerance = 0.0, smoothing = 0.0, offset = 0.0;
    if (!ReadEdit(this, IDC_SURF_TOLERANCE, tolerance) || !ReadEdit(this, IDC_SURF_SMOOTHING, smoothing) ||
        !ReadEdit(this, IDC_SURF_OFFSET, offset))
        return;
    if (offset < 0.0 || offset > 10000.0)
    {
        SetStatus(L"The offset must be from 0 to 10000 mm.");
        return;
    }
    if (tolerance < 0.001 || tolerance > 5.0)
    {
        SetStatus(L"The tolerance must be from 0.001 to 5 mm.");
        return;
    }
    if (smoothing < 0.0 || smoothing > 1.0)
    {
        SetStatus(L"The smoothing must be from 0 to 1.");
        return;
    }
    o.tolerance = tolerance;
    o.smoothing = smoothing;
    o.offset = offset;

    const scancore::SurfaceFitOptions& was = pScan->surface.options;
    if (o.base == was.base && o.symmetryXY == was.symmetryXY && o.symmetryYZ == was.symmetryYZ &&
        o.symmetryZX == was.symmetryZX && o.perimeter == was.perimeter && o.tolerance == was.tolerance &&
        o.smoothing == was.smoothing && o.offset == was.offset)
        return;
    pScan->surface.options = o;
    ShowSymmetryChoices();
    SetStatus(L"");
    Refit();
}

void CSurfaceDlg::OnCreate()
{
    AFX_MANAGE_STATE(AfxGetStaticModuleState());
    ScanDocument* pScan = FindScan(m_sessionId);
    if (pScan == nullptr || !pScan->surface.result.ok || theApp.m_pRoot == nullptr)
        return;
    const scancore::SurfaceFitResult& r = pScan->surface.result;
    const bool trimmed = !r.outline.empty();
    const auto start = std::chrono::steady_clock::now();

    // Write the file beside the log.
    const int number = ++pScan->surface.surfaces;
    wchar_t name[64];
    swprintf_s(name, L"scan-surface-%d.igs", number);
    const std::wstring path = LogFolderPath() + name;
    scancore::IgesHeader header;
    for (const wchar_t* c = name; *c != L'\0'; ++c)
        header.fileName += static_cast<char>(*c);      // plain ASCII
    header.timestamp = Timestamp();
    std::string message;
    const std::string bytes = trimmed ? scancore::WriteIgesTrimmedSurface(r.surface, r.outlineUV, r.outline, header)
                                      : scancore::WriteIgesSurface(r.surface, header);
    if (!scancore::WriteFileAtomically(path, bytes, message))
    {
        Log(L"[surface] %s", Widen(message).c_str());
        SetStatus(Widen(message).c_str());
        MessageBeep(MB_ICONWARNING);
        return;
    }
    Log(L"[surface] wrote %s (%s, %d x %d control points, %zu outline points)", path.c_str(),
        trimmed ? L"trimmed" : L"untrimmed", r.surface.countU, r.surface.countV, r.outline.size());

    SetStatus(L"Inserting the surface...");
    CWaitCursor wait;
    try
    {
        IADSessionPtr session = theApp.m_pRoot->GetSessions()->GetItem(_variant_t(m_sessionId.c_str()));
        IADPartSessionPtr part = session;
        if (part == nullptr)
        {
            SetStatus(L"Surfaces can only be inserted into a part.");
            return;
        }
        IADDesignSurfacesPtr surfaces = part->GetDesignSurfaces();
        const long before = surfaces->GetCount();
        IObjectCollectorPtr inserted = surfaces->InsertFromFile(
            _bstr_t(path.c_str()), ADFaceProcessingType_AD_STITCH_ADJOINING_FACES, VARIANT_TRUE /* heal */,
            VARIANT_FALSE /* make tolerant */, ADUnits_AD_MILLIMETERS);
        const long returned = (inserted != nullptr) ? inserted->GetCount() : -1;
        const long after = part->GetDesignSurfaces()->GetCount();
        const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();

        CString surfaceName = L"(none)";
        long faces = 0, edges = 0;
        if (after > before)
        {
            IADDesignSurfacePtr designSurface = part->GetDesignSurfaces()->GetItem(_variant_t(after - 1));
            if (designSurface != nullptr)
            {
                surfaceName = static_cast<LPCWSTR>(designSurface->GetName());
                faces = designSurface->GetFaces()->GetCount();
                edges = designSurface->GetEdges()->GetCount();
            }
        }
        Log(L"[surface] InsertFromFile returned %ld objects; design surfaces %ld to %ld; %s: %ld faces, %ld edges; "
            L"%.2f s",
            returned, before, after, static_cast<LPCWSTR>(surfaceName), faces, edges, seconds);

        CString status;
        if (after > before && faces > 0)
            status.Format(L"%s inserted (%ld face, %ld edges)%s in %.1f s. Use it with Alibre's Extrude To "
                          L"Geometry.",
                          static_cast<LPCWSTR>(surfaceName), faces, edges,
                          trimmed ? L", trimmed to the outline" : L"", seconds);
        else
            status.Format(L"Alibre didn't make a surface from %s.%s", name,
                          trimmed ? L" Try Use scan bounding rectangle: trimmed surfaces aren't proven yet." : L"");
        SetStatus(status);
        if (!(after > before && faces > 0))
            MessageBeep(MB_ICONWARNING);
    }
    catch (const _com_error& error)
    {
        const _bstr_t description = error.Description();
        Log(L"[surface] InsertFromFile failed: hr=0x%08X %s", static_cast<unsigned>(error.Error()),
            description.length() > 0 ? static_cast<LPCWSTR>(description) : L"");
        CString status;
        status.Format(L"Inserting the surface failed (hr=0x%08X) %s%s", static_cast<unsigned>(error.Error()),
                      description.length() > 0 ? static_cast<LPCWSTR>(description) : L"",
                      trimmed ? L" Try Use scan bounding rectangle: trimmed surfaces aren't proven yet." : L"");
        SetStatus(status);
        MessageBeep(MB_ICONWARNING);
    }
    RefreshScanViews();
}
