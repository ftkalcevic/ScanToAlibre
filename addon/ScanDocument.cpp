// ScanDocument.cpp : scans loaded into part workspaces.

#include "stdafx.h"
#include "ScanDocument.h"
#include "Log.h"

#include <algorithm>
#include <chrono>
#include <limits>
#include <cmath>

namespace
{
    std::map<std::wstring, std::unique_ptr<ScanDocument>> g_scans;
    std::map<std::wstring, std::string> g_unread;

    // The sample add-ons pass 1-based arrays, so these do too.
    template <class T>
    SAFEARRAY* MakeArray(VARTYPE type, const std::vector<T>& values)
    {
        SAFEARRAY* pArray = SafeArrayCreateVector(type, 1, static_cast<ULONG>(values.size()));
        if (pArray == nullptr)
            return nullptr;
        void* pData = nullptr;
        if (FAILED(SafeArrayAccessData(pArray, &pData)))
        {
            SafeArrayDestroy(pArray);
            return nullptr;
        }
        memcpy(pData, values.data(), values.size() * sizeof(T));
        SafeArrayUnaccessData(pArray);
        return pArray;
    }

    std::unique_ptr<ScanDocument::DrawArrays> MakeDrawArrays(const scancore::Mesh& mesh, double millimetresPerUnit)
    {
        const std::vector<scancore::Point3> normals = scancore::VertexNormals(mesh);
        std::vector<float> v, n;
        v.reserve(mesh.vertices.size() * 3);
        n.reserve(mesh.vertices.size() * 3);
        for (size_t i = 0; i < mesh.vertices.size(); ++i)
        {
            const scancore::Point3& p = mesh.vertices[i];
            v.push_back(static_cast<float>(p.x / millimetresPerUnit));
            v.push_back(static_cast<float>(p.y / millimetresPerUnit));
            v.push_back(static_cast<float>(p.z / millimetresPerUnit));
            n.push_back(static_cast<float>(normals[i].x));
            n.push_back(static_cast<float>(normals[i].y));
            n.push_back(static_cast<float>(normals[i].z));
        }
        std::vector<int> f;
        f.reserve(mesh.triangles.size() * 4);
        for (const auto& t : mesh.triangles)
        {
            f.push_back(3);
            f.push_back(t[0]);
            f.push_back(t[1]);
            f.push_back(t[2]);
        }

        auto arrays = std::make_unique<ScanDocument::DrawArrays>();
        arrays->vertices = MakeArray(VT_R4, v);
        arrays->normals = MakeArray(VT_R4, n);
        arrays->faces = MakeArray(VT_I4, f);
        arrays->triangles = mesh.triangles.size();
        return arrays;
    }
}

ScanDocument::DrawArrays::~DrawArrays()
{
    for (SAFEARRAY* pArray : { vertices, normals, faces })
    {
        if (pArray != nullptr)
            SafeArrayDestroy(pArray);
    }
}

void ScanDocument::SetMillimetresPerUnit(double millimetresPerUnit)
{
    if (millimetresPerUnit != m_millimetresPerUnit)
    {
        m_millimetresPerUnit = millimetresPerUnit;
        displayArrays.reset();
        fullArrays.reset();
        selectionArrays.reset();
        ResetBands();
    }
}

const ScanDocument::DrawArrays* ScanDocument::Arrays()
{
    std::unique_ptr<DrawArrays>& arrays = fullDetail ? fullArrays : displayArrays;
    if (arrays == nullptr)
    {
        const DWORD start = GetTickCount();
        arrays = MakeDrawArrays(fullDetail ? mesh : display, m_millimetresPerUnit);
        Log(L"[scan] made the %s drawing arrays (%zu triangles) in %lu ms", fullDetail ? L"full" : L"simplified",
            arrays->triangles, GetTickCount() - start);
    }
    if (arrays->vertices == nullptr || arrays->normals == nullptr || arrays->faces == nullptr)
        return nullptr;
    return arrays.get();
}

const ScanDocument::DrawArrays* ScanDocument::SelectionArrays()
{
    if (selection.empty())
        return nullptr;
    if (selectionArrays == nullptr)
    {
        // Just the chosen triangles, with their own vertices.
        scancore::Mesh part;
        std::map<int, int> map;
        for (int t : selection)
        {
            std::array<int, 3> tri;
            for (int k = 0; k < 3; ++k)
            {
                const int v = mesh.triangles[t][k];
                const auto found = map.find(v);
                if (found != map.end())
                {
                    tri[k] = found->second;
                }
                else
                {
                    tri[k] = static_cast<int>(part.vertices.size());
                    map.emplace(v, tri[k]);
                    part.vertices.push_back(mesh.vertices[v]);
                }
            }
            part.triangles.push_back(tri);
        }
        selectionArrays = MakeDrawArrays(part, m_millimetresPerUnit);
    }
    if (selectionArrays->vertices == nullptr || selectionArrays->normals == nullptr || selectionArrays->faces == nullptr)
        return nullptr;
    return selectionArrays.get();
}

const std::vector<std::array<int, 3>>& ScanDocument::Neighbours()
{
    if (m_neighbours.size() != mesh.triangles.size())
        m_neighbours = scancore::TriangleNeighbours(mesh);
    return m_neighbours;
}

void ScanDocument::SetSelection(std::vector<int> triangles)
{
    selection = std::move(triangles);
    selectionArrays.reset();
}

void ScanDocument::UpdateSeries()
{
    Series& s = series;
    s.result = scancore::SeriesResult();
    if (!s.plane.havePlane)
        return;
    const scancore::Point3 normal = scancore::Normalised(s.plane.normal);
    const scancore::SectionFrame base = scancore::FrameForPlane(s.plane.point, normal);
    s.plane.frame = base;
    if (!s.haveRange || scancore::Length(s.rangePoint - s.plane.point) > 1e-9 ||
        scancore::Length(s.rangeNormal - normal) > 1e-9)
    {
        scancore::DefaultSeriesRange(mesh, base, s.first, s.last);
        s.first = scancore::RoundTo(s.first, 0.01);
        s.last = scancore::RoundTo(s.last, 0.01);
        s.offsets = scancore::EvenOffsets(s.first, s.last, s.count);
        s.haveRange = true;
        s.rangePoint = s.plane.point;
        s.rangeNormal = normal;
        s.rangeFromPick = true;
        s.chosenRow = -1;
    }
    if (!scancore::ValidateOffsets(s.offsets, s.invalid))
        return;
    scancore::SeriesOptions options = s.options;
    options.tolerance = s.plane.tolerance;
    options.fitArcs = s.plane.snap.fitArcs;
    s.result = scancore::SliceSeries(mesh, base, s.offsets, options);
    Log(L"[series] %S along (%.4f, %.4f, %.4f): %zu planes, %S; %zu chains, %zu open polylines, %.2f s",
        s.plane.planeName.c_str(), normal.x, normal.y, normal.z, s.offsets.size(),
        scancore::DescribeRuns(s.result).c_str(), s.result.chains.size(), s.result.openPolylines, s.result.seconds);
    for (const std::string& note : s.result.notes)
        Log(L"[series] %S", note.c_str());
}

void ScanDocument::UpdateSection()
{
    if (seriesing)
    {
        UpdateSeries();
        return;
    }
    Section* pSection = ActiveSection();
    if (pSection == nullptr)
        return;
    Section& s = *pSection;
    if (!s.havePlane)
    {
        s.result = scancore::SectionResult();
        s.constraints.clear();
        return;
    }
    const scancore::Point3 origin = s.point + s.normal * s.offset;
    s.frame = scancore::FrameForPlane(origin, s.normal);
    SetSymmetryLine(s, s.frame);
    s.result = SectionOf(mesh, s, s.frame);
    s.constraints = scancore::ApplyDesignIntent(s.result, s.frame, s.tolerance, s.snap);
    if (s.snap.symmetric)
        Log(L"[section] symmetric about %S: %zu loops cut at the line, %zu pairs of loops, sides up to %.4f mm "
            L"apart, %zu symmetric constraints",
            s.symmetryName.empty() ? "(no symmetry plane square to this one)" : s.symmetryName.c_str(),
            s.result.symmetricSplit, s.result.symmetricPaired, s.result.symmetricGap,
            static_cast<size_t>(std::count_if(s.constraints.begin(), s.constraints.end(), [](const auto& c) {
                return c.kind == scancore::ProfileConstraint::Kind::Symmetric;
            })));
}

void ScanDocument::SetSymmetryLine(Section& s, const scancore::SectionFrame& frame) const
{
    using scancore::Point3;
    s.snap.haveSymmetryLine = false;
    s.symmetryName.clear();
    std::vector<scancore::Feature> planes;
    for (const scancore::Feature& f : fitted)
        if (f.kind == scancore::Feature::Kind::Plane && f.symmetry)
            planes.push_back(f);
    const char* worldNames[3] = { "XY", "YZ", "ZX" };
    const Point3 worldNormals[3] = { { 0, 0, 1 }, { 1, 0, 0 }, { 0, 1, 0 } };
    for (int k = 0; k < 3; ++k)
        if (detectedSymmetry[k])
        {
            scancore::Feature f;
            f.kind = scancore::Feature::Kind::Plane;
            f.direction = worldNormals[k];
            f.name = worldNames[k];
            planes.push_back(f);
        }
    const Point3 normal = frame.Normal();
    for (const scancore::Feature& f : planes)
    {
        const Point3 n = scancore::Normalised(f.direction);
        if (std::fabs(scancore::Dot(n, normal)) > std::sin(1.0 * 3.14159265358979323846 / 180.0))
            continue;
        // In the section plane, from its origin across to the symmetry plane.
        const Point3 across = scancore::Normalised(n - normal * scancore::Dot(n, normal));
        const double t = scancore::Dot(f.point - frame.origin, n) / scancore::Dot(across, n);
        const Point3 onBoth = frame.origin + across * t;
        const Point3 along = scancore::Normalised(scancore::Cross(n, normal));
        s.snap.haveSymmetryLine = true;
        s.snap.symmetryPoint = frame.ToPlane(onBoth);
        s.snap.symmetryDirection = { scancore::Dot(along, frame.xAxis), scancore::Dot(along, frame.yAxis) };
        s.symmetryName = f.name;
        return;
    }
}

scancore::SectionResult SectionOf(const scancore::Mesh& mesh, ScanDocument::Section& section,
                                  const scancore::SectionFrame& frame)
{
    if (section.kind == ScanDocument::Section::Kind::Silhouette)
    {
        const auto start = std::chrono::steady_clock::now();
        scancore::SectionResult result =
            scancore::SilhouetteMesh(mesh, frame, section.tolerance, kReferenceTolerance, section.smallestHole,
                                     &section.silhouetteInfo, section.snap.fitArcs);
        const scancore::SilhouetteInfo& i = section.silhouetteInfo;
        Log(L"[silhouette] %zu pieces, %zu holes (%zu smaller filled, %zu specks left out), cell %.3f mm, %zu of %zu "
            L"points on the mesh's outline edges, largest deviation %.4f mm, %.0f ms",
            i.pieces, i.holes, i.filledHoles, i.droppedPieces, i.cell, i.snapped, i.points, result.largestDeviation,
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
        return result;
    }
    return scancore::SectionMesh(mesh, frame, section.tolerance, kReferenceTolerance, section.snap.fitArcs);
}

void ScanDocument::UpdateSurface()
{
    if (selection.empty())
    {
        surface.result = scancore::SurfaceFitResult();
        return;
    }
    // The box round the scan, for the Box perimeter.
    const auto corners = box.Corners();
    surface.options.boxCorners.assign(corners.begin(), corners.end());
    surface.result = scancore::FitScanSurface(mesh, selection, surface.options);
    const scancore::SurfaceFitResult& r = surface.result;
    if (r.ok)
        Log(L"[surface] fitted %zu triangles (%zu samples): %d x %d control points, largest %.4f mm, 99%% within "
            L"%.4f, RMS %.4f; outline %zu points; %.2f s",
            selection.size(), r.samples, r.surface.countU, r.surface.countV, r.largest, r.within99, r.rms,
            r.outline.size(), r.seconds);
    else
        Log(L"[surface] no fit for %zu triangles: %S", selection.size(), r.message.c_str());
}

void DeviationColour(int band, long rgb[3])
{
    static const long colours[scancore::kDeviationBands][3] = {
        { 20, 40, 150 },   { 40, 110, 230 }, { 120, 190, 250 }, { 60, 190, 80 },
        { 240, 220, 40 },  { 245, 140, 30 }, { 215, 35, 35 },   { 150, 150, 150 },
    };
    const int b = (band >= 0 && band < scancore::kDeviationBands) ? band : scancore::kDeviationGrey;
    for (int k = 0; k < 3; ++k)
        rgb[k] = colours[b][k];
}

void ScanDocument::ResetBands()
{
    for (auto& arrays : m_bandArrays)
        arrays.reset();
    m_bandsMade = false;
}

void ScanDocument::UpdateCompare()
{
    compare.meshValues.clear();
    compare.displayValues.clear();
    compare.searched = 0.0;
    compare.haveProbe = false;
    if (compare.havePart && !compare.part.triangles.empty())
    {
        const auto start = std::chrono::steady_clock::now();
        compare.meshValues = scancore::Deviations(mesh.vertices, compare.part, compare.range);
        compare.displayValues = scancore::Deviations(display.vertices, compare.part, compare.range);
        compare.searched = compare.range;
        Log(L"[compare] %zu scan points (and %zu drawn) against %zu part triangles within %g mm in %.0f ms",
            mesh.vertices.size(), display.vertices.size(), compare.part.triangles.size(), compare.range,
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
    }
    RebandCompare();
}

void ScanDocument::RebandCompare()
{
    // Values beyond the range (after it shrinks) count as not compared.
    std::vector<float> values = compare.meshValues;
    for (float& v : values)
        if (std::fabs(v) > compare.range)
            v = std::numeric_limits<float>::quiet_NaN();
    compare.stats = scancore::DeviationStatistics(values, compare.tolerance);
    if (compare.haveProbe && std::fabs(compare.probeValue) > compare.range)
        compare.probeValue = std::numeric_limits<double>::quiet_NaN();
    const scancore::DeviationStats& s = compare.stats;
    if (!compare.meshValues.empty())
        Log(L"[compare] tolerance %g, range %g mm: %zu compared, %zu not; largest +%.4f / %.4f mm; rms %.4f mm; "
            L"%.1f%% within",
            compare.tolerance, compare.range, s.compared, s.notCompared, s.largestPositive, s.largestNegative, s.rms,
            100.0 * s.within);
    ResetBands();
}

const ScanDocument::DrawArrays* ScanDocument::BandArrays(int band)
{
    if (band < 0 || band >= scancore::kDeviationBands)
        return nullptr;
    if (!m_bandsMade || m_bandsFull != fullDetail)
    {
        ResetBands();
        m_bandsMade = true;
        m_bandsFull = fullDetail;
        const scancore::Mesh& source = fullDetail ? mesh : display;
        const std::vector<float>& values = fullDetail ? compare.meshValues : compare.displayValues;
        if (values.size() == source.vertices.size() && !values.empty())
        {
            scancore::Mesh parts[scancore::kDeviationBands];
            std::vector<int> remap(source.vertices.size() * scancore::kDeviationBands, -1);
            for (const auto& t : source.triangles)
            {
                const float a = values[t[0]], b = values[t[1]], c = values[t[2]];
                const double mean = (std::isnan(a) || std::isnan(b) || std::isnan(c))
                                        ? std::numeric_limits<double>::quiet_NaN()
                                        : (static_cast<double>(a) + b + c) / 3.0;
                const int k = scancore::DeviationBand(mean, compare.tolerance, compare.range);
                scancore::Mesh& part = parts[k];
                std::array<int, 3> tri;
                for (int corner = 0; corner < 3; ++corner)
                {
                    int& index = remap[static_cast<size_t>(t[corner]) * scancore::kDeviationBands + k];
                    if (index < 0)
                    {
                        index = static_cast<int>(part.vertices.size());
                        part.vertices.push_back(source.vertices[t[corner]]);
                    }
                    tri[corner] = index;
                }
                part.triangles.push_back(tri);
            }
            for (int k = 0; k < scancore::kDeviationBands; ++k)
                if (!parts[k].triangles.empty())
                    m_bandArrays[k] = MakeDrawArrays(parts[k], m_millimetresPerUnit);
        }
    }
    const DrawArrays* pArrays = m_bandArrays[band].get();
    if (pArrays == nullptr || pArrays->vertices == nullptr || pArrays->normals == nullptr || pArrays->faces == nullptr)
        return nullptr;
    return pArrays;
}

std::vector<scancore::Feature> ScanDocument::Features() const
{
    std::vector<scancore::Feature> features = scancore::BoxFeatures(box);
    features.insert(features.end(), fitted.begin(), fitted.end());
    return features;
}

void ScanDocument::Remember()
{
    m_undo.push_back({ mesh, box, fitted, align, picked });
    if (m_undo.size() > kUndoSteps)
        m_undo.erase(m_undo.begin());
}

void ScanDocument::DrawingChanged(bool trianglesChanged)
{
    bounds = scancore::Bounds(mesh);
    if (trianglesChanged)
    {
        display = scancore::SimplifyForDisplay(mesh, kDisplayTriangles);
        m_neighbours.clear();
        SetSelection({});
    }
    surface.result = scancore::SurfaceFitResult();
    // Compare's distances were for the scan as it was.
    compare.meshValues.clear();
    compare.displayValues.clear();
    compare.searched = 0.0;
    compare.stats = scancore::DeviationStats();
    compare.haveProbe = false;
    ResetBands();
    displayArrays.reset();
    fullArrays.reset();
    selectionArrays.reset();
    unsaved = true;
}

void ScanDocument::ReplaceMesh(scancore::Mesh&& changed)
{
    Remember();
    mesh = std::move(changed);
    box = scancore::FitOrientedBox(mesh);
    chosen = -1;
    DrawingChanged(true);
}

void ScanDocument::Move(const scancore::Transform& transform, const scancore::AlignState& newAlign)
{
    Remember();
    scancore::TransformMesh(mesh, transform);
    scancore::TransformMesh(display, transform);
    box.ApplyTransform(transform);
    for (scancore::Feature& plane : fitted)
        plane.ApplyTransform(transform);
    for (scancore::Point3& p : picked)
        p = transform.Apply(p);
    align = newAlign;
    DrawingChanged(false);
}

void ScanDocument::AddFitted(const scancore::Feature& feature)
{
    Remember();
    fitted.push_back(feature);
    unsaved = true;     // saved with the scan
}

void ScanDocument::DeleteFitted(size_t index)
{
    if (index >= fitted.size())
        return;
    Remember();
    fitted.erase(fitted.begin() + static_cast<std::ptrdiff_t>(index));
    chosen = -1;
    shownFitted = -1;
    unsaved = true;     // saved with the scan
}

void ScanDocument::DeleteAllFitted()
{
    if (fitted.empty())
        return;
    Remember();
    fitted.clear();
    chosen = -1;
    shownFitted = -1;
    unsaved = true;     // saved with the scan
}

bool ScanDocument::RenameFitted(size_t index, const std::string& name)
{
    if (index >= fitted.size() || name.empty() || fitted[index].name == name)
        return false;
    Remember();
    fitted[index].name = name;
    unsaved = true;     // saved with the scan
    return true;
}

void ScanDocument::Undo()
{
    if (m_undo.empty())
        return;
    Snapshot& last = m_undo.back();
    const bool trianglesChanged = last.mesh.triangles != mesh.triangles;
    mesh = std::move(last.mesh);
    box = last.box;
    fitted = std::move(last.fitted);
    align = last.align;
    picked = std::move(last.picked);
    m_undo.pop_back();
    chosen = -1;
    shownFitted = -1;
    if (!trianglesChanged)
        display = scancore::SimplifyForDisplay(mesh, kDisplayTriangles);    // the old shape, same triangles
    DrawingChanged(trianglesChanged);
}

std::unique_ptr<ScanDocument> MakeScanDocument(scancore::Mesh&& mesh, const std::string& sourceName)
{
    const DWORD start = GetTickCount();
    auto scan = std::make_unique<ScanDocument>();
    scan->sourceName = sourceName;
    scan->mesh = std::move(mesh);
    scan->bounds = scancore::Bounds(scan->mesh);
    scan->display = scancore::SimplifyForDisplay(scan->mesh, kDisplayTriangles);
    scan->box = scancore::FitOrientedBox(scan->mesh);

    const scancore::Point3 size = scan->bounds.Size();
    Log(L"[scan] %zu triangles, %zu vertices; box (%.2f, %.2f, %.2f) to (%.2f, %.2f, %.2f) mm, size %.2f x %.2f x %.2f; "
        L"simplified to %zu triangles; fitted box %.2f x %.2f x %.2f; in %lu ms",
        scan->mesh.triangles.size(), scan->mesh.vertices.size(), scan->bounds.low.x, scan->bounds.low.y,
        scan->bounds.low.z, scan->bounds.high.x, scan->bounds.high.y, scan->bounds.high.z, size.x, size.y, size.z,
        scan->display.triangles.size(), 2 * scan->box.halfSize[0], 2 * scan->box.halfSize[1],
        2 * scan->box.halfSize[2], GetTickCount() - start);
    return scan;
}

ScanDocument* FindScan(const std::wstring& sessionId)
{
    const auto found = g_scans.find(sessionId);
    return (found != g_scans.end()) ? found->second.get() : nullptr;
}

ScanDocument* AddScan(const std::wstring& sessionId, std::unique_ptr<ScanDocument> scan)
{
    ScanDocument* pScan = scan.get();
    g_scans[sessionId] = std::move(scan);
    return pScan;
}

void ClearScans()
{
    g_scans.clear();
    g_unread.clear();
}

void KeepUnreadData(const std::wstring& sessionId, std::string bytes)
{
    g_unread[sessionId] = std::move(bytes);
}

const std::string* FindUnreadData(const std::wstring& sessionId)
{
    const auto found = g_unread.find(sessionId);
    return (found != g_unread.end()) ? &found->second : nullptr;
}

void ForgetUnreadData(const std::wstring& sessionId)
{
    g_unread.erase(sessionId);
}
