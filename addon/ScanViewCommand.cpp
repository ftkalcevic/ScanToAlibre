// ScanViewCommand.cpp : draws a part's scan and picks points on it.

#include "stdafx.h"
#include "ScanToAlibre.h"
#include "ScanViewCommand.h"
#include "ScanDocument.h"
#include "Log.h"

#include "CompareDialog.h"
#include "OrientDialog.h"
#include "SectionDialog.h"
#include "SurfaceDialog.h"
#include "ScanDisplayCommand.h"
#include "StepDialog.h"

#include "Orient.h"
#include "Section.h"
#include "ViewProjection.h"

#include <chrono>
#include <cstdlib>
#include <algorithm>
#include <cmath>
#include <iterator>
#include <limits>
#include <set>
#include <vector>

using scancore::Feature;
using scancore::Point3;

namespace
{
    double Milliseconds(std::chrono::steady_clock::time_point since)
    {
        return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - since).count();
    }

    __int64 AddSegment(IADAddOnCanvasDisplay* pCanvas, const wchar_t* name, long red, long green, long blue,
                       bool foreground)
    {
        __int64 segment = 0;
        pCanvas->AddSubSegment(0, _bstr_t(name), &segment);
        pCanvas->SetSegmentColor(segment, red, green, blue, 255);
        pCanvas->ToggleForegroundRendering(segment, foreground ? VARIANT_TRUE : VARIANT_FALSE);
        return segment;
    }

    // A point through the canvas's WorldToScreen, in model units in and whatever
    // screen units come out. Returns false if the call fails.
    bool WorldToScreen(IADAddOnCommandSite* pSite, const Point3& modelPoint, scancore::ScreenPoint& screen)
    {
        SAFEARRAY* pWorld = SafeArrayCreateVector(VT_R8, 0, 3);
        if (pWorld == nullptr)
            return false;
        double* pData = nullptr;
        SafeArrayAccessData(pWorld, reinterpret_cast<void**>(&pData));
        pData[0] = modelPoint.x;
        pData[1] = modelPoint.y;
        pData[2] = modelPoint.z;
        SafeArrayUnaccessData(pWorld);

        SAFEARRAY* pScreen = nullptr;
        const HRESULT hr = pSite->WorldToScreen(&pWorld, &pScreen);
        SafeArrayDestroy(pWorld);
        if (FAILED(hr) || pScreen == nullptr)
            return false;

        // The sample reads two elements without saying their type, so read either.
        VARTYPE type = VT_EMPTY;
        SafeArrayGetVartype(pScreen, &type);
        long lower = 0;
        SafeArrayGetLBound(pScreen, 1, &lower);
        double values[2] = {};
        for (long i = 0; i < 2; ++i)
        {
            long index = lower + i;
            if (type == VT_R8)
                SafeArrayGetElement(pScreen, &index, &values[i]);
            else if (type == VT_R4)
            {
                float f = 0.0f;
                SafeArrayGetElement(pScreen, &index, &f);
                values[i] = f;
            }
            else
            {
                long l = 0;
                SafeArrayGetElement(pScreen, &index, &l);
                values[i] = l;
            }
        }
        SafeArrayDestroy(pScreen);
        screen = { values[0], values[1] };
        return true;
    }

    // The sample add-ons pass 1-based arrays, so this does too.
    void DrawPolyline(IADAddOnCanvasDisplay* pCanvas, __int64 segment, const std::vector<Point3>& points, double unit)
    {
        std::vector<float> values;
        for (const Point3& p : points)
        {
            values.push_back(static_cast<float>(p.x / unit));
            values.push_back(static_cast<float>(p.y / unit));
            values.push_back(static_cast<float>(p.z / unit));
        }
        SAFEARRAY* pArray = SafeArrayCreateVector(VT_R4, 1, static_cast<ULONG>(values.size()));
        if (pArray == nullptr)
            return;
        void* pData = nullptr;
        if (SUCCEEDED(SafeArrayAccessData(pArray, &pData)))
        {
            memcpy(pData, values.data(), values.size() * sizeof(float));
            SafeArrayUnaccessData(pArray);
            __int64 result = 0;
            pCanvas->DrawPolyline(segment, &pArray, &result);
        }
        SafeArrayDestroy(pArray);
    }

    void DrawMeshArrays(IADAddOnCanvasDisplay* pCanvas, __int64 segment, const ScanDocument::DrawArrays& arrays)
    {
        SAFEARRAY* pVertices = arrays.vertices;
        SAFEARRAY* pNormals = arrays.normals;
        SAFEARRAY* pFaces = arrays.faces;
        __int64 result = 0;
        pCanvas->DrawMesh(segment, &pVertices, &pNormals, &pFaces, &result);
    }

    // Two directions in a plane, square to each other: along the box's other
    // axes when the plane is square to one of them, so its outline sits square.
    void PlaneDirections(const Feature& plane, const scancore::OrientedBox& box, Point3& u, Point3& v)
    {
        for (int k = 0; k < 3; ++k)
        {
            if (std::fabs(scancore::Dot(plane.direction, box.axes[k])) > 0.999)
            {
                u = box.axes[(k + 1) % 3];
                v = box.axes[(k + 2) % 3];
                return;
            }
        }
        // Otherwise along the box axis most nearly in the plane, laid into it, so a
        // fitted plane's outline still lines up with the box.
        int most = 0;
        for (int k = 1; k < 3; ++k)
            if (std::fabs(scancore::Dot(plane.direction, box.axes[k])) <
                std::fabs(scancore::Dot(plane.direction, box.axes[most])))
                most = k;
        const Point3 along = box.axes[most] - plane.direction * scancore::Dot(box.axes[most], plane.direction);
        u = scancore::Normalised(along);
        v = scancore::Cross(plane.direction, u);
    }

    // Colours: the box's A, B and C directions red, green and blue; fitted
    // planes orange; the chosen one yellow.
    void FeatureColour(const Feature& feature, int index, bool chosen, long rgb[3])
    {
        static const long kColours[4][3] = { { 220, 60, 60 }, { 60, 170, 60 }, { 60, 110, 235 }, { 245, 140, 20 } };
        static const long kChosen[3] = { 255, 215, 0 };
        const long* c = kColours[3];
        if (index < 7)
        {
            const char letter = feature.name.empty() ? 'A' : feature.name.back();
            c = (letter == 'B') ? kColours[1] : (letter == 'C') ? kColours[2] : (letter == 'A') ? kColours[0]
                                                                                                : kColours[3];
        }
        if (chosen)
            c = kChosen;
        rgb[0] = c[0];
        rgb[1] = c[1];
        rgb[2] = c[2];
    }

    double DistanceToSegment(double x, double y, const scancore::ScreenPoint& a, const scancore::ScreenPoint& b)
    {
        const double dx = b.u - a.u, dy = b.v - a.v;
        const double length2 = dx * dx + dy * dy;
        double t = (length2 > 0.0) ? ((x - a.u) * dx + (y - a.v) * dy) / length2 : 0.0;
        t = std::clamp(t, 0.0, 1.0);
        return std::hypot(x - (a.u + t * dx), y - (a.v + t * dy));
    }
}

namespace
{
    std::set<CScanViewCommand*> g_views;
}

void RefreshScanViews()
{
    for (CScanViewCommand* pView : g_views)
        pView->Refresh();
    RefreshScanDisplays();
}

void RefreshAlibreButtons()
{
    static bool logged = false;
    bool any = false;
    for (CScanViewCommand* pView : g_views)
    {
        if (pView->Site() == nullptr)
            continue;
        const HRESULT hr = pView->Site()->UpdateToolbars();
        any = true;
        if (!logged)
        {
            logged = true;
            Log(L"[changed] UpdateToolbars hr=0x%08X", static_cast<unsigned>(hr));
        }
    }
    if (!any)
        Log(L"[changed] no scan view running to refresh Alibre's buttons through");
}

CScanViewCommand::CScanViewCommand(const std::wstring& sessionId)
    : CCommandBase(L"scan view")
    , m_sessionId(sessionId)
{
    g_views.insert(this);
}

CScanViewCommand::~CScanViewCommand()
{
    g_views.erase(this);
}

void CScanViewCommand::Refresh()
{
    m_renders = 0;
    if (m_site != nullptr)
        m_site->InvalidateCanvas();
}

STDMETHODIMP CScanViewCommand::On3DRender()
{
    AFX_MANAGE_STATE(AfxGetStaticModuleState());

    ScanDocument* pScan = FindScan(m_sessionId);
    if (m_site == nullptr || pScan == nullptr)
        return S_OK;

    const auto start = std::chrono::steady_clock::now();

    // Alibre loses the graphics on pan, zoom and hover unless they're rebuilt on every render.
    IUnknown* pContext = nullptr;
    const HRESULT hr = m_site->Begin3DDisplay(VARIANT_TRUE, &pContext);
    if (!m_reportedRender)
        Log(L"[view] first On3DRender: Begin3DDisplay hr=0x%08X", static_cast<unsigned>(hr));
    if (FAILED(hr))
        return S_OK;

    if (pContext != nullptr)
    {
        IADAddOnCanvasDisplayPtr canvas;
        pContext->QueryInterface(__uuidof(IADAddOnCanvasDisplay), reinterpret_cast<void**>(&canvas));
        pContext->Release();

        if (canvas != nullptr)
        {
            try
            {
                // Compare: the scan in colour bands, drawn over the part so what's
                // inside it shows too (in place of the plain scan).
                bool banded = false;
                if (pScan->comparing)
                {
                    static const wchar_t* const kBandNames[scancore::kDeviationBands] = {
                        L"Compare far inside", L"Compare inside", L"Compare just inside", L"Compare within tolerance",
                        L"Compare just outside", L"Compare outside", L"Compare far outside", L"Compare not compared"
                    };
                    for (int band = 0; band < scancore::kDeviationBands; ++band)
                    {
                        const ScanDocument::DrawArrays* pBand = pScan->BandArrays(band);
                        if (pBand == nullptr)
                            continue;
                        long rgb[3];
                        DeviationColour(band, rgb);
                        DrawMeshArrays(canvas, AddSegment(canvas, kBandNames[band], rgb[0], rgb[1], rgb[2], true),
                                       *pBand);
                        banded = true;
                    }
                    if (pScan->compare.haveProbe)
                    {
                        const __int64 probe = AddSegment(canvas, L"Compare probe", 230, 30, 230, true);
                        const double unit = pScan->MillimetresPerUnit();
                        const Point3& p = pScan->compare.probe;
                        canvas->DrawMarker(probe, static_cast<float>(p.x / unit), static_cast<float>(p.y / unit),
                                           static_cast<float>(p.z / unit), 16, MarkerType_MARKER_ASTERISK);
                    }
                }

                // Show Scan draws the scan while it's on; this draws only what's
                // over it. With Show Scan off, this draws the scan only while one
                // of our dialogs is open for the part and the user hasn't hidden
                // it, as this command outlives the dialog (until a native command
                // such as Measure ends it) and kept the scan showing.
                const CStepDialog* pDialog = OpenStepDialog();
                const bool dialogOpen = pDialog != nullptr && pDialog->SessionId() == m_sessionId;
                const ScanDocument::DrawArrays* pArrays =
                    (banded || ScanDisplayOn(m_sessionId) || pScan->scanHidden || !dialogOpen) ? nullptr
                                                                                                 : pScan->Arrays();
                if (pArrays != nullptr)
                {
                    // Hidden by the model, like a part, so features built on it show.
                    const __int64 mesh = AddSegment(canvas, L"Scan", 150, 175, 205, false);
                    SAFEARRAY* pVertices = pArrays->vertices;
                    SAFEARRAY* pNormals = pArrays->normals;
                    SAFEARRAY* pFaces = pArrays->faces;
                    __int64 result = 0;
                    const HRESULT meshHr = canvas->DrawMesh(mesh, &pVertices, &pNormals, &pFaces, &result);
                    if (FAILED(meshHr) && !m_reportedRender)
                        Log(L"[view] DrawMesh failed hr=0x%08X", static_cast<unsigned>(meshHr));
                }

                if (pScan->orienting)
                    DrawOrient(canvas, *pScan, false);
                else if (pScan->Sectioning() && pScan->clickMode == ScanDocument::ClickMode::Scan)
                    DrawOrient(canvas, *pScan, true);
                if (pScan->Sectioning())
                    DrawSection(canvas, *pScan);
                if (pScan->surfacing)
                    DrawSurface(canvas, *pScan);

                if (!pScan->picked.empty())
                {
                    const __int64 markers = AddSegment(canvas, L"Picked points", 230, 60, 30, true);
                    const double unit = pScan->MillimetresPerUnit();
                    for (const Point3& p : pScan->picked)
                    {
                        const HRESULT markerHr = canvas->DrawMarker(
                            markers, static_cast<float>(p.x / unit), static_cast<float>(p.y / unit),
                            static_cast<float>(p.z / unit), 12, MarkerType_MARKER_CROSSHAIR);
                        if (FAILED(markerHr) && !m_reportedRender)
                            Log(L"[view] DrawMarker failed hr=0x%08X", static_cast<unsigned>(markerHr));
                    }
                }
            }
            catch (const _com_error& error)
            {
                Log(L"[view] drawing failed: hr=0x%08X", static_cast<unsigned>(error.Error()));
            }
        }
    }

    m_site->End3DDisplay();

    // The time to hand the scan over, for the first few renders.
    if (++m_renders <= 5)
        Log(L"[view] render %d (%s): %.0f ms", m_renders, pScan->fullDetail ? L"full detail" : L"simplified",
            Milliseconds(start));
    m_reportedRender = true;
    return S_OK;
}

STDMETHODIMP CScanViewCommand::OnClick(long screenX, long screenY, enum ADDONMouseButtons buttons,
                                       VARIANT_BOOL* pHandled)
{
    AFX_MANAGE_STATE(AfxGetStaticModuleState());

    if (pHandled == nullptr)
        return E_POINTER;
    *pHandled = VARIANT_FALSE;

    ScanDocument* pScan = FindScan(m_sessionId);
    if (pScan == nullptr || m_site == nullptr || buttons != ADDONMouseButtons_ADDON_LEFT_BUTTON)
        return S_OK;

    try
    {
        if (!m_probedScreenToWorld)
        {
            m_probedScreenToWorld = true;
            ProbeScreenToWorld(screenX, screenY);
        }
        bool handled = false;
        if (pScan->clickMode == ScanDocument::ClickMode::Scan && (pScan->orienting || pScan->Sectioning()))
            handled = PickFeature(*pScan, screenX, screenY);
        else if (pScan->orienting && pScan->clickMode == ScanDocument::ClickMode::MidPlane)
        {
            handled = PickFeature(*pScan, screenX, screenY);
            if (handled)
                OrientMidPlanePicked();
        }
        else if (pScan->orienting && (pScan->clickMode == ScanDocument::ClickMode::Area ||
                                      pScan->clickMode == ScanDocument::ClickMode::Cylinder))
            handled = GrowAt(*pScan, screenX, screenY);
        else if (pScan->surfacing && (pScan->clickMode == ScanDocument::ClickMode::Smooth ||
                                      pScan->clickMode == ScanDocument::ClickMode::Brush))
            handled = SelectSurfaceAt(*pScan, screenX, screenY);
        else if (pScan->comparing && pScan->clickMode == ScanDocument::ClickMode::Probe)
            handled = ProbeAt(*pScan, screenX, screenY);
        else if (!pScan->orienting && !pScan->Sectioning() && !pScan->surfacing && !pScan->comparing)
            handled = Pick(*pScan, screenX, screenY);
        // A hit is ours; anything else is left to Alibre, which selects what's
        // there (with To down, its planes, axes and points become the target in
        // OnSelectionChange).
        if (handled)
            *pHandled = VARIANT_TRUE;
    }
    catch (const _com_error& error)
    {
        Log(L"[pick] failed: hr=0x%08X", static_cast<unsigned>(error.Error()));
    }
    return S_OK;
}

bool CScanViewCommand::FitView(const ScanDocument& scan, scancore::ViewProjection& view)
{
    const double unit = scan.MillimetresPerUnit();
    scancore::Box3 extent = scan.bounds;
    for (const Point3& corner : scan.box.Corners())
        extent.Add(corner);
    const std::vector<Point3> samples = scancore::ProjectionSamplePoints(extent);
    std::vector<scancore::ScreenPoint> screen;
    for (const Point3& p : samples)
    {
        scancore::ScreenPoint s;
        if (!WorldToScreen(m_site, p / unit, s))
        {
            Log(L"[pick] WorldToScreen failed");
            return false;
        }
        screen.push_back(s);
    }
    if (!view.Fit(samples, screen))
    {
        Log(L"[pick] the view's projection couldn't be fitted");
        return false;
    }
    return true;
}

bool CScanViewCommand::PickFeature(ScanDocument& scan, long x, long y)
{
    scancore::ViewProjection view;
    if (!FitView(scan, view))
        return false;
    const scancore::Ray line = view.LineThrough(x, y);
    const std::vector<Feature> features = scan.Features();

    // Axes, points and planes' drawn lines (outline and cross) by how close they
    // pass on screen, so a plane behind another or inside the scan can be had by
    // clicking its lines. Failing that, planes by where the line meets them inside
    // their outline: the nearest, or if that's already chosen, the next one behind,
    // so clicking again in the same place goes deeper.
    int best = -1;
    double bestPixels = 8.0;
    std::vector<std::pair<double, int>> inside;     // depth, feature
    for (size_t i = 0; i < features.size(); ++i)
    {
        const Feature& f = features[i];
        if (scan.Sectioning() && f.kind != Feature::Kind::Plane)
            continue;
        if (f.kind == Feature::Kind::Axis)
        {
            const double pixels = DistanceToSegment(x, y, view.Project(f.point - f.direction * f.halfWidth),
                                                    view.Project(f.point + f.direction * f.halfWidth));
            if (pixels < bestPixels)
            {
                bestPixels = pixels;
                best = static_cast<int>(i);
            }
        }
        else if (f.kind == Feature::Kind::Point)
        {
            const scancore::ScreenPoint s = view.Project(f.point);
            const double pixels = std::hypot(x - s.u, y - s.v);
            if (pixels < bestPixels + 2.0)
            {
                bestPixels = pixels;
                best = static_cast<int>(i);
            }
        }
        else
        {
            Point3 u, v;
            PlaneDirections(f, scan.box, u, v);
            const double h = f.halfWidth;
            const Point3 c = f.point;
            const Point3 corners[4] = { c + (u + v) * h, c + (v - u) * h, c - (u + v) * h, c + (u - v) * h };
            const Point3 ends[6][2] = { { corners[0], corners[1] }, { corners[1], corners[2] },
                                        { corners[2], corners[3] }, { corners[3], corners[0] },
                                        { c - u * h, c + u * h },   { c - v * h, c + v * h } };
            for (const auto& e : ends)
            {
                const double pixels = DistanceToSegment(x, y, view.Project(e[0]), view.Project(e[1]));
                if (pixels < bestPixels)
                {
                    bestPixels = pixels;
                    best = static_cast<int>(i);
                }
            }

            const double along = scancore::Dot(line.direction, f.direction);
            if (std::fabs(along) < 1e-9)
                continue;
            const double depth = scancore::Dot(f.point - line.origin, f.direction) / along;
            const Point3 local = line.origin + line.direction * depth - f.point;
            if (std::fabs(scancore::Dot(local, u)) <= h && std::fabs(scancore::Dot(local, v)) <= h)
                inside.emplace_back(depth, static_cast<int>(i));
        }
    }
    if (best < 0 && !inside.empty())
    {
        std::sort(inside.begin(), inside.end());
        size_t k = 0;
        for (size_t j = 0; j < inside.size(); ++j)
            if (inside[j].second == scan.chosen)
                k = (j + 1) % inside.size();
        best = inside[k].second;
    }
    if (best < 0)
    {
        Log(L"[orient] click (%ld, %ld) on none of the scan's planes, axes or centre; left to Alibre", x, y);
        return false;
    }
    scan.chosen = best;
    if (scan.Sectioning())
    {
        const Feature& f = features[best];
        ScanDocument::Section& section = *scan.ActiveSection();
        section.havePlane = true;
        section.planeName = f.name;
        section.point = f.point;
        section.normal = scancore::Normalised(f.direction);
        section.alibrePlane = nullptr;
        section.alibreFace = false;
        const auto start = std::chrono::steady_clock::now();
        scan.UpdateSection();
        Log(L"[section] plane %S: %zu polylines, %zu points, largest deviation %.4f mm, %.0f ms", f.name.c_str(),
            section.result.profiles.size(), section.result.slicePoints, section.result.largestDeviation,
            Milliseconds(start));
        scan.clickMode = ScanDocument::ClickMode::None;     // one pick a press
        SectionPlanePicked();
    }
    else
    {
        Log(L"[orient] chose %S", features[best].name.c_str());
        OrientViewChanged();
    }
    m_site->InvalidateCanvas();
    return true;
}

bool CScanViewCommand::ProbeAt(ScanDocument& scan, long x, long y)
{
    scancore::ViewProjection view;
    if (!FitView(scan, view))
        return false;
    const scancore::RayHit hit = scancore::IntersectLine(scan.mesh, view.LineThrough(x, y));
    if (!hit.hit)
        return false;
    ScanDocument::Compare& c = scan.compare;
    const auto& tri = scan.mesh.triangles[static_cast<size_t>(hit.triangle)];
    double value = std::numeric_limits<double>::quiet_NaN();
    if (c.meshValues.size() == scan.mesh.vertices.size())
    {
        // The corners' distances, weighted by where the click is in the triangle.
        const Point3& a = scan.mesh.vertices[static_cast<size_t>(tri[0])];
        const Point3& b = scan.mesh.vertices[static_cast<size_t>(tri[1])];
        const Point3& d = scan.mesh.vertices[static_cast<size_t>(tri[2])];
        const Point3 normal = scancore::Cross(b - a, d - a);
        const double area = scancore::Dot(normal, normal);
        if (area > 0.0)
        {
            const double wa = scancore::Dot(scancore::Cross(b - hit.point, d - hit.point), normal) / area;
            const double wb = scancore::Dot(scancore::Cross(d - hit.point, a - hit.point), normal) / area;
            const double wc = 1.0 - wa - wb;
            const float va = c.meshValues[static_cast<size_t>(tri[0])];
            const float vb = c.meshValues[static_cast<size_t>(tri[1])];
            const float vc = c.meshValues[static_cast<size_t>(tri[2])];
            if (!std::isnan(va) && !std::isnan(vb) && !std::isnan(vc))
                value = wa * va + wb * vb + wc * vc;
        }
    }
    if (std::fabs(value) > c.range)
        value = std::numeric_limits<double>::quiet_NaN();
    c.haveProbe = true;
    c.probe = hit.point;
    c.probeValue = value;
    Log(L"[compare] probe at (%.3f, %.3f, %.3f) on triangle %d: %s%.4f mm", hit.point.x, hit.point.y, hit.point.z,
        hit.triangle, std::isnan(value) ? L"not modelled " : L"", std::isnan(value) ? 0.0 : value);
    CompareViewChanged();
    m_site->InvalidateCanvas();
    return true;
}

bool CScanViewCommand::GrowAt(ScanDocument& scan, long x, long y)
{
    scancore::ViewProjection view;
    if (!FitView(scan, view))
        return false;
    const scancore::RayHit hit = scancore::IntersectLine(scan.mesh, view.LineThrough(x, y));
    if (!hit.hit)
        return false;

    const auto start = std::chrono::steady_clock::now();
    const bool cylinder = scan.clickMode == ScanDocument::ClickMode::Cylinder;
    const std::vector<int> area =
        cylinder ? scancore::GrowCylinder(scan.mesh, scan.Neighbours(), hit.triangle, scan.growAngle, scan.growDistance)
                 : scancore::GrowFlatArea(scan.mesh, scan.Neighbours(), hit.triangle, scan.growAngle, scan.growDistance);
    const bool remove = std::binary_search(scan.selection.begin(), scan.selection.end(), hit.triangle);
    std::vector<int> selection;
    if (remove)
        std::set_difference(scan.selection.begin(), scan.selection.end(), area.begin(), area.end(),
                            std::back_inserter(selection));
    else
        std::set_union(scan.selection.begin(), scan.selection.end(), area.begin(), area.end(),
                       std::back_inserter(selection));
    Log(L"[orient] %s %zu triangles of a %s grown from triangle %d (within %g degrees and %g mm); %zu selected, "
        L"%.0f ms",
        remove ? L"removed" : L"added", area.size(), cylinder ? L"cylinder" : L"flat area", hit.triangle,
        scan.growAngle, scan.growDistance, selection.size(), Milliseconds(start));
    scan.SetSelection(std::move(selection));
    OrientViewChanged();
    m_site->InvalidateCanvas();
    return true;
}

bool CScanViewCommand::SelectSurfaceAt(ScanDocument& scan, long x, long y)
{
    scancore::ViewProjection view;
    if (!FitView(scan, view))
        return false;
    const scancore::RayHit hit = scancore::IntersectLine(scan.mesh, view.LineThrough(x, y));
    if (!hit.hit)
        return false;

    const auto start = std::chrono::steady_clock::now();
    const bool brush = scan.clickMode == ScanDocument::ClickMode::Brush;
    const std::vector<int> area =
        brush ? scancore::BrushArea(scan.mesh, scan.Neighbours(), hit.triangle, hit.point, scan.surface.brushRadius)
              : scancore::GrowSmoothArea(scan.mesh, scan.Neighbours(), hit.triangle, scan.surface.smoothAngle);
    const bool remove = std::binary_search(scan.selection.begin(), scan.selection.end(), hit.triangle);
    std::vector<int> selection;
    if (remove)
        std::set_difference(scan.selection.begin(), scan.selection.end(), area.begin(), area.end(),
                            std::back_inserter(selection));
    else
        std::set_union(scan.selection.begin(), scan.selection.end(), area.begin(), area.end(),
                       std::back_inserter(selection));
    Log(L"[surface] %s %zu triangles (%s from triangle %d); %zu selected, %.0f ms",
        remove ? L"removed" : L"added", area.size(),
        brush ? L"brush" : L"smooth area", hit.triangle, selection.size(), Milliseconds(start));
    scan.SetSelection(std::move(selection));
    {
        CWaitCursor wait;
        scan.UpdateSurface();
    }
    SurfaceViewChanged();
    m_site->InvalidateCanvas();
    return true;
}

void CScanViewCommand::DrawSurface(IADAddOnCanvasDisplay* pCanvas, ScanDocument& scan)
{
    const double unit = scan.MillimetresPerUnit();
    if (const ScanDocument::DrawArrays* pSelection = scan.SelectionArrays())
        DrawMeshArrays(pCanvas, AddSegment(pCanvas, L"Surface selection", 245, 140, 20, true), *pSelection);

    const scancore::SurfaceFitResult& r = scan.surface.result;
    if (!r.ok)
        return;

    // The surface as 21 lines each way, green.
    const int kLines = 20, kSteps = 60;
    const __int64 gridSegment = AddSegment(pCanvas, L"Fitted surface", 40, 190, 70, true);
    for (int k = 0; k <= kLines; ++k)
    {
        const double a = static_cast<double>(k) / kLines;
        std::vector<Point3> alongU, alongV;
        for (int i = 0; i <= kSteps; ++i)
        {
            const double b = static_cast<double>(i) / kSteps;
            alongU.push_back(scancore::Evaluate(r.surface, b, a));
            alongV.push_back(scancore::Evaluate(r.surface, a, b));
        }
        DrawPolyline(pCanvas, gridSegment, alongU, unit);
        DrawPolyline(pCanvas, gridSegment, alongV, unit);
    }

    // The perimeter, thicker yellow: the outline it's trimmed to, or for an
    // untrimmed rectangle its edges, along the surface.
    std::vector<Point3> loop = r.outline;
    if (loop.empty())
    {
        const int kEdgeSteps = 60;
        const double corners[5][2] = { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 }, { 0, 0 } };
        for (int side = 0; side < 4; ++side)
            for (int i = 0; i < kEdgeSteps; ++i)
            {
                const double f = static_cast<double>(i) / kEdgeSteps;
                const double u = corners[side][0] + (corners[side + 1][0] - corners[side][0]) * f;
                const double v = corners[side][1] + (corners[side + 1][1] - corners[side][1]) * f;
                loop.push_back(scancore::Evaluate(r.surface, u, v));
            }
    }
    const __int64 outlineSegment = AddSegment(pCanvas, L"Surface outline", 245, 200, 40, true);
    pCanvas->SetLineWeight(outlineSegment, 3.0f);
    loop.push_back(loop.front());
    DrawPolyline(pCanvas, outlineSegment, loop, unit);
}

void CScanViewCommand::DrawOrient(IADAddOnCanvasDisplay* pCanvas, ScanDocument& scan, bool planesOnly)
{
    const double unit = scan.MillimetresPerUnit();

    // The selected triangles, over the scan.
    const ScanDocument::DrawArrays* pSelection = planesOnly ? nullptr : scan.SelectionArrays();
    if (pSelection != nullptr)
        DrawMeshArrays(pCanvas, AddSegment(pCanvas, L"Selected area", 245, 140, 20, true), *pSelection);

    // The fitted box's edges.
    const auto corners = scan.box.Corners();
    const __int64 boxSegment = AddSegment(pCanvas, L"Fitted box", 130, 130, 130, true);
    static const int kEdges[12][2] = { { 0, 1 }, { 2, 3 }, { 4, 5 }, { 6, 7 }, { 0, 2 }, { 1, 3 },
                                       { 4, 6 }, { 5, 7 }, { 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 } };
    for (const auto& edge : kEdges)
        DrawPolyline(pCanvas, boxSegment, { corners[edge[0]], corners[edge[1]] }, unit);

    // Planes as outlines with a cross, axes as lines, the centre as a marker.
    const std::vector<Feature> features = scan.Features();
    for (size_t i = 0; i < features.size(); ++i)
    {
        const Feature& f = features[i];
        if (planesOnly && f.kind != Feature::Kind::Plane)
            continue;
        const bool chosen = static_cast<int>(i) == scan.chosen;
        long rgb[3];
        FeatureColour(f, static_cast<int>(i), chosen, rgb);
        DrawScanFeature(pCanvas, scan, f, rgb, chosen);
    }
}

void DrawScanFeature(IADAddOnCanvasDisplay* pCanvas, const ScanDocument& scan, const Feature& f, const long rgb[3],
                     bool chosen)
{
    const double unit = scan.MillimetresPerUnit();
    const std::wstring name(f.name.begin(), f.name.end());
    const __int64 segment = AddSegment(pCanvas, name.c_str(), rgb[0], rgb[1], rgb[2], true);
    pCanvas->SetLineWeight(segment, chosen ? 3.0f : (f.kind == Feature::Kind::Plane ? 1.0f : 2.0f));
    if (f.kind == Feature::Kind::Plane)
    {
        Point3 u, v;
        PlaneDirections(f, scan.box, u, v);
        const double h = f.halfWidth;
        const Point3 c = f.point;
        DrawPolyline(pCanvas, segment,
                     { c + (u + v) * h, c + (v - u) * h, c - (u + v) * h, c + (u - v) * h, c + (u + v) * h }, unit);
        DrawPolyline(pCanvas, segment, { c - u * h, c + u * h }, unit);
        DrawPolyline(pCanvas, segment, { c - v * h, c + v * h }, unit);
    }
    else if (f.kind == Feature::Kind::Axis)
    {
        DrawPolyline(pCanvas, segment, { f.point - f.direction * f.halfWidth, f.point + f.direction * f.halfWidth },
                     unit);
    }
    else
    {
        pCanvas->DrawMarker(segment, static_cast<float>(f.point.x / unit), static_cast<float>(f.point.y / unit),
                            static_cast<float>(f.point.z / unit), chosen ? 16 : 10, MarkerType_MARKER_ASTERISK);
    }
}

void CScanViewCommand::DrawSection(IADAddOnCanvasDisplay* pCanvas, ScanDocument& scan)
{
    const ScanDocument::Section* pSection = scan.ActiveSection();
    if (pSection == nullptr || !pSection->havePlane)
        return;
    const ScanDocument::Section& section = *pSection;
    const double unit = scan.MillimetresPerUnit();
    const scancore::SectionFrame& frame = section.frame;
    using scancore::Vec2;
    auto world = [&](const Vec2& p) { return frame.ToWorld(p); };

    // The plane: a square a little bigger than the scan, round the scan's centre.
    const scancore::Box3& b = scan.bounds;
    const double half = 0.6 * scancore::Length(b.high - b.low);
    const Vec2 middle = frame.ToPlane(b.Centre());
    const __int64 planeSegment = AddSegment(pCanvas, L"Section plane", 245, 200, 40, true);
    DrawPolyline(pCanvas, planeSegment,
                 { world({ middle.x - half, middle.y - half }), world({ middle.x + half, middle.y - half }),
                   world({ middle.x + half, middle.y + half }), world({ middle.x - half, middle.y + half }),
                   world({ middle.x - half, middle.y - half }) },
                 unit);

    // The slice, thin red.
    const __int64 sliceSegment = AddSegment(pCanvas, L"Section slice", 220, 50, 50, true);
    for (const scancore::SlicePolyline& slice : section.result.slices)
    {
        std::vector<Point3> points = slice.points;
        if (slice.closed && !points.empty())
            points.push_back(points.front());
        DrawPolyline(pCanvas, sliceSegment, points, unit);
    }

    // The fit, thicker green: arcs and circles drawn every 3 degrees.
    const __int64 fitSegment = AddSegment(pCanvas, L"Section fit", 40, 190, 70, true);
    pCanvas->SetLineWeight(fitSegment, 2.0f);
    const double kPi = 3.14159265358979323846;
    for (const scancore::Profile& profile : section.result.profiles)
    {
        for (const scancore::ProfileSegment& seg : profile.segments)
        {
            std::vector<Point3> points;
            if (seg.kind == scancore::ProfileSegment::Kind::Line)
            {
                points = { world(seg.start), world(seg.end) };
            }
            else
            {
                double from = 0.0, sweep = 2.0 * kPi;
                if (seg.kind == scancore::ProfileSegment::Kind::Arc)
                {
                    from = std::atan2(seg.start.y - seg.centre.y, seg.start.x - seg.centre.x);
                    const double to = std::atan2(seg.end.y - seg.centre.y, seg.end.x - seg.centre.x);
                    sweep = to - from;
                    if (seg.anticlockwise && sweep < 0.0)
                        sweep += 2.0 * kPi;
                    else if (!seg.anticlockwise && sweep > 0.0)
                        sweep -= 2.0 * kPi;
                }
                const int steps = (std::max)(2, static_cast<int>(std::ceil(std::fabs(sweep) / (3.0 * kPi / 180.0))));
                for (int k = 0; k <= steps; ++k)
                {
                    const double a = from + sweep * k / steps;
                    points.push_back(world({ seg.centre.x + seg.radius * std::cos(a),
                                             seg.centre.y + seg.radius * std::sin(a) }));
                }
            }
            DrawPolyline(pCanvas, fitSegment, points, unit);
        }
    }
}

void CScanViewCommand::ReadSectionPlane(ScanDocument& scan)
{
    IADSessionPtr session = theApp.m_pRoot->GetSessions()->GetItem(_variant_t(m_sessionId.c_str()));
    IADPointPtr clicked;
    IObjectCollectorPtr selected = session->SelectedObjectsEx(&clicked);
    const long count = (selected != nullptr) ? selected->GetCount() : 0;
    if (count == 0)
        return;
    IDispatchPtr item = selected->GetItem(_variant_t(0L));
    IADTargetProxyPtr proxy = item;
    IDispatchPtr target = (proxy != nullptr) ? proxy->GetTarget() : item;
    const double unit = scan.MillimetresPerUnit();

    ScanDocument::Section* pSection = scan.ActiveSection();
    if (pSection == nullptr)
        return;
    ScanDocument::Section& section = *pSection;
    Point3 point, normal;
    std::string name;
    bool face = false;
    if (IADDesignPlanePtr plane = target)
    {
        IADPointPtr p1, p2, p3;
        plane->GetGeometry(&p1, &p2, &p3);
        IADVectorPtr n = plane->GetNormal();
        if (p1 == nullptr || n == nullptr)
            return;
        point = Point3{ p1->GetX(), p1->GetY(), p1->GetZ() } * unit;
        normal = { n->GetX(), n->GetY(), n->GetZ() };
        name = static_cast<const char*>(_bstr_t(plane->GetName()));
    }
    else if (IADFacePtr alibreFace = target)
    {
        IADSurfacePtr surface = alibreFace->GetGeometry();
        IADPlanePtr plane = surface;
        if (plane == nullptr)
        {
            Log(L"[section] the selected face isn't flat");
            SectionViewChanged();
            return;
        }
        IADPointPtr root = plane->GetRootPoint();
        IADVectorPtr n = plane->GetNormal();
        if (root == nullptr || n == nullptr)
            return;
        point = Point3{ root->GetX(), root->GetY(), root->GetZ() } * unit;
        normal = { n->GetX(), n->GetY(), n->GetZ() };
        // Outward from the part, so a positive offset goes out of it (unproven).
        if (alibreFace->GetIsSenseReversed() != VARIANT_FALSE)
            normal = normal * -1.0;
        name = "a flat face";
        face = true;
    }
    else
    {
        // What it is, by its type information's name, to learn what Alibre sends.
        std::wstring typeName = L"unknown";
        ITypeInfo* pInfo = nullptr;
        if (target != nullptr && SUCCEEDED(target->GetTypeInfo(0, LOCALE_USER_DEFAULT, &pInfo)) && pInfo != nullptr)
        {
            BSTR name = nullptr;
            if (SUCCEEDED(pInfo->GetDocumentation(MEMBERID_NIL, &name, nullptr, nullptr, nullptr)) && name != nullptr)
            {
                typeName = name;
                SysFreeString(name);
            }
            pInfo->Release();
        }
        Log(L"[section] Alibre's selection isn't a design plane or a face (proxy %d, %ld selected, type %s)",
            proxy != nullptr ? 1 : 0, count, typeName.c_str());
        return;
    }
    section.havePlane = true;
    section.planeName = name;
    section.point = point;
    section.normal = scancore::Normalised(normal);
    section.alibrePlane = target;
    section.alibreFace = face;
    scan.chosen = -1;
    const auto start = std::chrono::steady_clock::now();
    scan.UpdateSection();
    Log(L"[section] %S: point (%.3f, %.3f, %.3f) mm, normal (%.4f, %.4f, %.4f); %zu polylines, %zu points, "
        L"largest deviation %.4f mm, %.0f ms",
        name.c_str(), point.x, point.y, point.z, section.normal.x, section.normal.y, section.normal.z,
        section.result.profiles.size(), section.result.slicePoints, section.result.largestDeviation,
        Milliseconds(start));
    scan.clickMode = ScanDocument::ClickMode::None;     // one pick a press
    SectionPlanePicked();
    if (m_site != nullptr)
        m_site->InvalidateCanvas();
}

STDMETHODIMP CScanViewCommand::OnSelectionChange()
{
    AFX_MANAGE_STATE(AfxGetStaticModuleState());

    ScanDocument* pScan = FindScan(m_sessionId);
    if (pScan != nullptr && pScan->Sectioning() && pScan->clickMode == ScanDocument::ClickMode::Target &&
        theApp.m_pRoot != nullptr)
    {
        try
        {
            ReadSectionPlane(*pScan);
        }
        catch (const _com_error& error)
        {
            Log(L"[section] reading Alibre's selection failed: hr=0x%08X", static_cast<unsigned>(error.Error()));
        }
        return S_OK;
    }
    if (pScan == nullptr || !pScan->orienting || pScan->clickMode != ScanDocument::ClickMode::Target ||
        theApp.m_pRoot == nullptr)
        return S_OK;

    try
    {
        IADSessionPtr session = theApp.m_pRoot->GetSessions()->GetItem(_variant_t(m_sessionId.c_str()));
        IADPointPtr clicked;
        IObjectCollectorPtr selected = session->SelectedObjectsEx(&clicked);
        const long count = (selected != nullptr) ? selected->GetCount() : 0;
        if (count == 0)
            return S_OK;

        // Design geometry may come as itself or inside a proxy.
        IDispatchPtr item = selected->GetItem(_variant_t(0L));
        IADTargetProxyPtr proxy = item;
        IDispatchPtr target = (proxy != nullptr) ? proxy->GetTarget() : item;
        const double unit = pScan->MillimetresPerUnit();

        Feature feature;
        bool read = false;
        if (IADDesignPlanePtr plane = target)
        {
            IADPointPtr p1, p2, p3;
            plane->GetGeometry(&p1, &p2, &p3);
            IADVectorPtr normal = plane->GetNormal();
            if (p1 != nullptr && normal != nullptr)
            {
                feature.kind = Feature::Kind::Plane;
                feature.point = Point3{ p1->GetX(), p1->GetY(), p1->GetZ() } * unit;
                feature.direction = scancore::Normalised({ normal->GetX(), normal->GetY(), normal->GetZ() });
                feature.name = static_cast<const char*>(_bstr_t(plane->GetName()));
                read = true;
            }
        }
        else if (IADDesignAxisPtr axis = target)
        {
            IADPointPtr p1, p2;
            axis->GetGeometry(&p1, &p2);
            IADVectorPtr direction = axis->GetDirection();
            if (p1 != nullptr && direction != nullptr)
            {
                feature.kind = Feature::Kind::Axis;
                feature.point = Point3{ p1->GetX(), p1->GetY(), p1->GetZ() } * unit;
                feature.direction = scancore::Normalised({ direction->GetX(), direction->GetY(), direction->GetZ() });
                feature.name = static_cast<const char*>(_bstr_t(axis->GetName()));
                read = true;
            }
        }
        else if (IADDesignPointPtr point = target)
        {
            IADPointPtr p = point->GetGeometry();
            if (p != nullptr)
            {
                feature.kind = Feature::Kind::Point;
                feature.point = Point3{ p->GetX(), p->GetY(), p->GetZ() } * unit;
                feature.name = static_cast<const char*>(_bstr_t(point->GetName()));
                read = true;
            }
        }

        if (!read)
        {
            Log(L"[orient] Alibre's selection isn't a design plane, axis or point (proxy %d)", proxy != nullptr ? 1 : 0);
            return S_OK;
        }
        pScan->target = feature;
        pScan->haveTarget = true;
        Log(L"[orient] target %S: point (%.3f, %.3f, %.3f) mm, direction (%.3f, %.3f, %.3f)", feature.name.c_str(),
            feature.point.x, feature.point.y, feature.point.z, feature.direction.x, feature.direction.y,
            feature.direction.z);
        OrientViewChanged();
    }
    catch (const _com_error& error)
    {
        Log(L"[orient] reading Alibre's selection failed: hr=0x%08X", static_cast<unsigned>(error.Error()));
    }
    return S_OK;
}

bool CScanViewCommand::Pick(ScanDocument& scan, long x, long y)
{
    const auto start = std::chrono::steady_clock::now();
    const double unit = scan.MillimetresPerUnit();

    // Fit the view's projection from the scan's box corners.
    const std::vector<Point3> samples = scancore::ProjectionSamplePoints(scan.bounds);
    std::vector<scancore::ScreenPoint> screen;
    for (const Point3& p : samples)
    {
        scancore::ScreenPoint s;
        if (!WorldToScreen(m_site, p / unit, s))
        {
            Log(L"[pick] WorldToScreen failed");
            return false;
        }
        screen.push_back(s);
    }
    scancore::ViewProjection view;
    if (!view.Fit(samples, screen))
    {
        Log(L"[pick] the view's projection couldn't be fitted");
        return false;
    }

    // OnClick's coordinates and WorldToScreen's are the same (Frank's log: every
    // click as given landed under the cursor). The line points into the screen, so
    // its first hit is the near side; flipping it from GetCameraPosition picked the
    // far side, so the camera is only logged, to learn what it means.
    const scancore::Ray line = view.LineThrough(x, y);
    if (!m_reportedCamera)
    {
        m_reportedCamera = true;
        try
        {
            IADSessionPtr session = theApp.m_pRoot->GetSessions()->GetItem(_variant_t(m_sessionId.c_str()));
            IADDesignSessionPtr design = session;
            IADPointPtr position = (design != nullptr) ? design->GetCameraPosition() : nullptr;
            if (position != nullptr)
            {
                const Point3 camera{ position->GetX(), position->GetY(), position->GetZ() };
                const Point3 towards = scancore::Normalised(scan.bounds.Centre() - camera * unit);
                Log(L"[probe] camera position (%g, %g, %g) model units; into the screen (%.3f, %.3f, %.3f); "
                    L"camera to scan (%.3f, %.3f, %.3f); perspective %d",
                    camera.x, camera.y, camera.z, line.direction.x, line.direction.y, line.direction.z, towards.x,
                    towards.y, towards.z, design->GetIsPerspective() != VARIANT_FALSE ? 1 : 0);
            }
        }
        catch (const _com_error& error)
        {
            Log(L"[probe] camera position failed: hr=0x%08X", static_cast<unsigned>(error.Error()));
        }
    }

    const scancore::RayHit hit = scancore::IntersectLine(scan.mesh, line);
    if (hit.hit)
    {
        scan.picked.push_back(hit.point);
        Log(L"[pick] click (%ld, %ld): (%.3f, %.3f, %.3f) mm on triangle %d; fit error %.3g, %.0f ms", x, y,
            hit.point.x, hit.point.y, hit.point.z, hit.triangle, view.LargestError(), Milliseconds(start));
        m_site->InvalidateCanvas();
        return true;
    }

    const scancore::ScreenPoint centre = view.Project(scan.bounds.Centre());
    Log(L"[pick] click (%ld, %ld) missed the scan; its centre is at (%.1f, %.1f) on screen; fit error %.3g", x, y,
        centre.u, centre.v, view.LargestError());
    return false;
}

void CScanViewCommand::ProbeScreenToWorld(long x, long y)
{
    // Undocumented: log what it gives for the first click, to learn it.
    SAFEARRAY* pIn = SafeArrayCreateVector(VT_R8, 0, 2);
    if (pIn == nullptr)
        return;
    double* pData = nullptr;
    SafeArrayAccessData(pIn, reinterpret_cast<void**>(&pData));
    pData[0] = x;
    pData[1] = y;
    SafeArrayUnaccessData(pIn);

    SAFEARRAY* pOut = nullptr;
    const HRESULT hr = m_site->ScreenToWorld(&pIn, &pOut);
    SafeArrayDestroy(pIn);
    if (FAILED(hr) || pOut == nullptr)
    {
        Log(L"[probe] ScreenToWorld(%ld, %ld) hr=0x%08X", x, y, static_cast<unsigned>(hr));
        return;
    }

    VARTYPE type = VT_EMPTY;
    SafeArrayGetVartype(pOut, &type);
    long lower = 0, upper = -1;
    SafeArrayGetLBound(pOut, 1, &lower);
    SafeArrayGetUBound(pOut, 1, &upper);
    std::wstring values;
    for (long i = lower; i <= upper && i < lower + 12; ++i)
    {
        double value = 0.0;
        if (type == VT_R8)
            SafeArrayGetElement(pOut, &i, &value);
        else if (type == VT_R4)
        {
            float f = 0.0f;
            SafeArrayGetElement(pOut, &i, &f);
            value = f;
        }
        wchar_t text[48];
        swprintf_s(text, L" %g", value);
        values += text;
    }
    Log(L"[probe] ScreenToWorld(%ld, %ld): type %d, %ld values:%s", x, y, static_cast<int>(type), upper - lower + 1,
        values.c_str());
    SafeArrayDestroy(pOut);
}

STDMETHODIMP CScanViewCommand::OnKeyDown(long keycode, VARIANT_BOOL* pHandled)
{
    AFX_MANAGE_STATE(AfxGetStaticModuleState());

    if (pHandled == nullptr)
        return E_POINTER;
    *pHandled = VARIANT_FALSE;

    ScanDocument* pScan = FindScan(m_sessionId);
    if (pScan == nullptr || m_site == nullptr)
        return S_OK;

    if (keycode == 'F')
    {
        // Full detail or simplified, to compare drawing speed.
        pScan->fullDetail = !pScan->fullDetail;
        m_renders = 0;
        Log(L"[view] drawing %s", pScan->fullDetail ? L"full detail" : L"simplified");
        m_site->InvalidateCanvas();
        *pHandled = VARIANT_TRUE;
    }
    else if (keycode == 'C')
    {
        pScan->picked.clear();
        Log(L"[pick] cleared");
        m_site->InvalidateCanvas();
        *pHandled = VARIANT_TRUE;
    }
    return S_OK;
}

STDMETHODIMP CScanViewCommand::OnTerminate()
{
    AFX_MANAGE_STATE(AfxGetStaticModuleState());
    Log(L"[view] OnTerminate: the scan is no longer drawn; Show Scan draws it again");
    m_site = nullptr;
    return S_OK;
}
