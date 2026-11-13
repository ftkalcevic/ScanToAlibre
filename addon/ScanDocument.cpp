// ScanDocument.cpp : scans loaded into part workspaces.

#include "stdafx.h"
#include "ScanDocument.h"
#include "Log.h"

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
