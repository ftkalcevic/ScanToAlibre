// ScanViewCommand.cpp : draws a part's scan and picks points on it.

#include "stdafx.h"
#include "ScanToAlibre.h"
#include "ScanViewCommand.h"
#include "ScanDocument.h"
#include "Log.h"

#include "OrientDialog.h"

#include "Orient.h"
#include "ViewProjection.h"

#include <chrono>
#include <cstdlib>
#include <algorithm>
#include <cmath>
#include <iterator>
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
        const Point3 other = (std::fabs(plane.direction.x) < 0.9) ? Point3{ 1, 0, 0 } : Point3{ 0, 1, 0 };
        u = scancore::Normalised(scancore::Cross(plane.direction, other));
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
                const ScanDocument::DrawArrays* pArrays = pScan->Arrays();
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
                    DrawOrient(canvas, *pScan);

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
        if (pScan->orienting)
            handled = pScan->selectMode ? GrowAt(*pScan, screenX, screenY) : PickFeature(*pScan, screenX, screenY);
        else
            handled = Pick(*pScan, screenX, screenY);
        // A hit is ours; a miss is left to Alibre, which selects what's there
        // (its planes, axes and points, read in OnSelectionChange while orienting).
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

    // Axes and points by how close they pass on screen; planes by where the
    // line meets them inside their outline, the nearest first. Lines win.
    int best = -1;
    double bestPixels = 8.0;
    int bestPlane = -1;
    double bestDepth = 1e300;
    for (size_t i = 0; i < features.size(); ++i)
    {
        const Feature& f = features[i];
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
            const double along = scancore::Dot(line.direction, f.direction);
            if (std::fabs(along) < 1e-9)
                continue;
            const double depth = scancore::Dot(f.point - line.origin, f.direction) / along;
            const Point3 local = line.origin + line.direction * depth - f.point;
            Point3 u, v;
            PlaneDirections(f, scan.box, u, v);
            if (std::fabs(scancore::Dot(local, u)) <= f.halfWidth && std::fabs(scancore::Dot(local, v)) <= f.halfWidth &&
                depth < bestDepth)
            {
                bestDepth = depth;
                bestPlane = static_cast<int>(i);
            }
        }
    }
    if (best < 0)
        best = bestPlane;
    if (best < 0)
    {
        Log(L"[orient] click (%ld, %ld) on none of the scan's planes, axes or centre; left to Alibre", x, y);
        return false;
    }
    scan.chosen = best;
    Log(L"[orient] chose %S", features[best].name.c_str());
    OrientViewChanged();
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
    const std::vector<int> area =
        scancore::GrowFlatArea(scan.mesh, scan.Neighbours(), hit.triangle, scan.growAngle, scan.growDistance);
    const bool remove = std::binary_search(scan.selection.begin(), scan.selection.end(), hit.triangle);
    std::vector<int> selection;
    if (remove)
        std::set_difference(scan.selection.begin(), scan.selection.end(), area.begin(), area.end(),
                            std::back_inserter(selection));
    else
        std::set_union(scan.selection.begin(), scan.selection.end(), area.begin(), area.end(),
                       std::back_inserter(selection));
    Log(L"[orient] %s %zu triangles grown from triangle %d (within %g degrees and %g mm); %zu selected, %.0f ms",
        remove ? L"removed" : L"added", area.size(), hit.triangle, scan.growAngle, scan.growDistance, selection.size(),
        Milliseconds(start));
    scan.SetSelection(std::move(selection));
    OrientViewChanged();
    m_site->InvalidateCanvas();
    return true;
}

void CScanViewCommand::DrawOrient(IADAddOnCanvasDisplay* pCanvas, ScanDocument& scan)
{
    const double unit = scan.MillimetresPerUnit();

    // The selected triangles, over the scan.
    if (const ScanDocument::DrawArrays* pSelection = scan.SelectionArrays())
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
        const bool chosen = static_cast<int>(i) == scan.chosen;
        long rgb[3];
        FeatureColour(f, static_cast<int>(i), chosen, rgb);
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
}

STDMETHODIMP CScanViewCommand::OnSelectionChange()
{
    AFX_MANAGE_STATE(AfxGetStaticModuleState());

    ScanDocument* pScan = FindScan(m_sessionId);
    if (pScan == nullptr || !pScan->orienting || pScan->selectMode || theApp.m_pRoot == nullptr)
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
