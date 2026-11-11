// Decimate.cpp : quadric edge collapse.

#include "Decimate.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <queue>
#include <unordered_map>
#include <vector>

namespace scancore
{
    namespace
    {
        // A symmetric 4 x 4 matrix: the sum of p p^T over planes p = (a, b, c, d).
        struct Quadric
        {
            double a2 = 0, ab = 0, ac = 0, ad = 0, b2 = 0, bc = 0, bd = 0, c2 = 0, cd = 0, d2 = 0;

            void AddPlane(const Point3& n, double d, double weight)
            {
                a2 += weight * n.x * n.x; ab += weight * n.x * n.y; ac += weight * n.x * n.z; ad += weight * n.x * d;
                b2 += weight * n.y * n.y; bc += weight * n.y * n.z; bd += weight * n.y * d;
                c2 += weight * n.z * n.z; cd += weight * n.z * d;
                d2 += weight * d * d;
            }
            void Add(const Quadric& q)
            {
                a2 += q.a2; ab += q.ab; ac += q.ac; ad += q.ad; b2 += q.b2;
                bc += q.bc; bd += q.bd; c2 += q.c2; cd += q.cd; d2 += q.d2;
            }
            double Evaluate(const Point3& p) const
            {
                const double x = p.x, y = p.y, z = p.z;
                return a2 * x * x + 2 * ab * x * y + 2 * ac * x * z + 2 * ad * x + b2 * y * y + 2 * bc * y * z +
                       2 * bd * y + c2 * z * z + 2 * cd * z + d2;
            }
            // The point minimising Evaluate, if the 3 x 3 part can be solved.
            bool Minimum(Point3& p) const
            {
                const double det = a2 * (b2 * c2 - bc * bc) - ab * (ab * c2 - bc * ac) + ac * (ab * bc - b2 * ac);
                // Singular when the planes are near parallel (a flat area) or all
                // share a line (a crease): compared with the trace cubed.
                const double trace = a2 + b2 + c2;
                if (!(std::fabs(det) > 1e-6 * trace * trace * trace))
                    return false;
                const double rx = -ad, ry = -bd, rz = -cd;
                const double dx = rx * (b2 * c2 - bc * bc) - ab * (ry * c2 - bc * rz) + ac * (ry * bc - b2 * rz);
                const double dy = a2 * (ry * c2 - bc * rz) - rx * (ab * c2 - bc * ac) + ac * (ab * rz - ry * ac);
                const double dz = a2 * (b2 * rz - ry * bc) - ab * (ab * rz - ry * ac) + rx * (ab * bc - b2 * ac);
                p = { dx / det, dy / det, dz / det };
                return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z);
            }
        };

        struct Candidate
        {
            double cost;
            int a, b;
            int versionA, versionB;
            bool operator<(const Candidate& other) const { return cost > other.cost; }     // smallest first
        };

        uint64_t EdgeKey(int a, int b)
        {
            if (a > b)
                std::swap(a, b);
            return (static_cast<uint64_t>(static_cast<uint32_t>(a)) << 32) | static_cast<uint32_t>(b);
        }

        class Decimator
        {
        public:
            Decimator(const Mesh& mesh, const DecimateOptions& options) : m_options(options)
            {
                m_points = mesh.vertices;
                m_triangles = mesh.triangles;
                const size_t vertexCount = m_points.size();
                m_alive.assign(m_triangles.size(), true);
                m_aliveCount = m_triangles.size();
                m_vertexAlive.assign(vertexCount, true);
                m_version.assign(vertexCount, 0);
                m_quadrics.assign(vertexCount, Quadric());
                m_vertexTriangles.assign(vertexCount, {});

                std::unordered_map<uint64_t, int> edgeUse;
                edgeUse.reserve(m_triangles.size() * 2);
                for (size_t t = 0; t < m_triangles.size(); ++t)
                {
                    const auto& tri = m_triangles[t];
                    for (int k = 0; k < 3; ++k)
                    {
                        m_vertexTriangles[tri[k]].push_back(static_cast<int>(t));
                        ++edgeUse[EdgeKey(tri[k], tri[(k + 1) % 3])];
                    }
                    const Point3 n = Normalised(Cross(m_points[tri[1]] - m_points[tri[0]],
                                                      m_points[tri[2]] - m_points[tri[0]]));
                    if (Length(n) == 0.0)
                        continue;
                    const double d = -Dot(n, m_points[tri[0]]);
                    for (int k = 0; k < 3; ++k)
                        m_quadrics[tri[k]].AddPlane(n, d, 1.0);
                }

                // Open edges: a plane through the edge, square to its triangle, so
                // the edge resists moving sideways.
                const double boundaryWeight = options.keepOpenEdges ? 100.0 : 1.0;
                for (const auto& tri : m_triangles)
                {
                    const Point3 faceNormal = Normalised(Cross(m_points[tri[1]] - m_points[tri[0]],
                                                               m_points[tri[2]] - m_points[tri[0]]));
                    for (int k = 0; k < 3; ++k)
                    {
                        const int a = tri[k], b = tri[(k + 1) % 3];
                        if (edgeUse[EdgeKey(a, b)] != 1)
                            continue;
                        const Point3 n = Normalised(Cross(m_points[b] - m_points[a], faceNormal));
                        if (Length(n) == 0.0)
                            continue;
                        const double d = -Dot(n, m_points[a]);
                        m_quadrics[a].AddPlane(n, d, boundaryWeight);
                        m_quadrics[b].AddPlane(n, d, boundaryWeight);
                    }
                }

                for (const auto& entry : edgeUse)
                {
                    const int a = static_cast<int>(entry.first >> 32);
                    const int b = static_cast<int>(entry.first & 0xffffffffu);
                    Push(a, b);
                }
            }

            Mesh Run(DecimateResult& result)
            {
                result.trianglesBefore = m_triangles.size();
                while (m_aliveCount > m_options.targetTriangles && !m_heap.empty())
                {
                    const Candidate c = m_heap.top();
                    m_heap.pop();
                    if (!m_vertexAlive[c.a] || !m_vertexAlive[c.b] || m_version[c.a] != c.versionA ||
                        m_version[c.b] != c.versionB)
                        continue;

                    const double error = std::sqrt(std::max(c.cost, 0.0));
                    if (m_options.maxError > 0.0 && error > m_options.maxError)
                    {
                        result.stoppedByError = true;
                        break;
                    }

                    Point3 target;
                    Placement(c.a, c.b, target);
                    if (!CanCollapse(c.a, c.b, target))
                    {
                        ++result.refused;
                        continue;
                    }
                    Collapse(c.a, c.b, target);
                    ++result.collapses;
                    result.largestError = std::max(result.largestError, error);
                }
                return Compact(result);
            }

        private:
            // The collapsed point for the edge, and its cost.
            double Placement(int a, int b, Point3& point) const
            {
                Quadric q = m_quadrics[a];
                q.Add(m_quadrics[b]);
                const Point3 pa = m_points[a], pb = m_points[b];
                const Point3 middle = (pa + pb) * 0.5;
                const double length = Length(pb - pa);

                Point3 best = middle;
                double bestCost = q.Evaluate(middle);
                Point3 optimal;
                if (q.Minimum(optimal) && Length(optimal - middle) <= 2.0 * length + 1e-12)
                {
                    const double cost = q.Evaluate(optimal);
                    if (cost < bestCost)
                    {
                        best = optimal;
                        bestCost = cost;
                    }
                }
                for (const Point3& end : { pa, pb })
                {
                    const double cost = q.Evaluate(end);
                    if (cost < bestCost)
                    {
                        best = end;
                        bestCost = cost;
                    }
                }
                point = best;
                return bestCost;
            }

            void Push(int a, int b)
            {
                Point3 point;
                const double cost = Placement(a, b, point);
                m_heap.push({ cost, a, b, m_version[a], m_version[b] });
            }

            bool Has(const std::array<int, 3>& tri, int v) const
            {
                return tri[0] == v || tri[1] == v || tri[2] == v;
            }

            void Neighbours(int v, std::vector<int>& out) const
            {
                out.clear();
                for (int t : m_vertexTriangles[v])
                {
                    if (!m_alive[t])
                        continue;
                    for (int k : m_triangles[t])
                        if (k != v)
                            out.push_back(k);
                }
                std::sort(out.begin(), out.end());
                out.erase(std::unique(out.begin(), out.end()), out.end());
            }

            bool CanCollapse(int a, int b, const Point3& target)
            {
                // The link condition: the two ends share only the vertices
                // opposite the edge, so the surface doesn't pinch.
                int edgeTriangles = 0;
                for (int t : m_vertexTriangles[a])
                    if (m_alive[t] && Has(m_triangles[t], b))
                        ++edgeTriangles;
                if (edgeTriangles == 0)
                    return false;
                Neighbours(a, m_scratchA);
                Neighbours(b, m_scratchB);
                size_t common = 0;
                for (size_t i = 0, j = 0; i < m_scratchA.size() && j < m_scratchB.size();)
                {
                    if (m_scratchA[i] < m_scratchB[j]) ++i;
                    else if (m_scratchA[i] > m_scratchB[j]) ++j;
                    else { ++common; ++i; ++j; }
                }
                if (common != static_cast<size_t>(edgeTriangles))
                    return false;

                // No triangle may fold over or shrink to nothing.
                for (int v : { a, b })
                {
                    for (int t : m_vertexTriangles[v])
                    {
                        if (!m_alive[t])
                            continue;
                        const auto& tri = m_triangles[t];
                        if (Has(tri, a) && Has(tri, b))
                            continue;
                        Point3 before[3], after[3];
                        for (int k = 0; k < 3; ++k)
                        {
                            before[k] = m_points[tri[k]];
                            after[k] = (tri[k] == a || tri[k] == b) ? target : before[k];
                        }
                        const Point3 n0 = Cross(before[1] - before[0], before[2] - before[0]);
                        const Point3 n1 = Cross(after[1] - after[0], after[2] - after[0]);
                        const double l0 = Length(n0), l1 = Length(n1);
                        if (l1 <= 1e-12 * (l0 + 1e-300))
                            return false;
                        if (Dot(n0, n1) < 0.1 * l0 * l1)
                            return false;
                    }
                }
                return true;
            }

            void Collapse(int a, int b, const Point3& target)
            {
                m_points[a] = target;
                m_quadrics[a].Add(m_quadrics[b]);
                for (int t : m_vertexTriangles[b])
                {
                    if (!m_alive[t])
                        continue;
                    auto& tri = m_triangles[t];
                    if (Has(tri, a))
                    {
                        m_alive[t] = false;
                        --m_aliveCount;
                        continue;
                    }
                    for (int& k : tri)
                        if (k == b)
                            k = a;
                    m_vertexTriangles[a].push_back(t);
                }
                m_vertexTriangles[b].clear();
                m_vertexTriangles[b].shrink_to_fit();
                m_vertexAlive[b] = false;
                ++m_version[a];

                auto& list = m_vertexTriangles[a];
                list.erase(std::remove_if(list.begin(), list.end(), [this](int t) { return !m_alive[t]; }), list.end());
                std::sort(list.begin(), list.end());
                list.erase(std::unique(list.begin(), list.end()), list.end());

                Neighbours(a, m_scratchA);
                for (int n : m_scratchA)
                    Push(a, n);
            }

            Mesh Compact(DecimateResult& result) const
            {
                Mesh out;
                std::vector<int> map(m_points.size(), -1);
                for (size_t t = 0; t < m_triangles.size(); ++t)
                {
                    if (!m_alive[t])
                        continue;
                    std::array<int, 3> tri;
                    for (int k = 0; k < 3; ++k)
                    {
                        const int v = m_triangles[t][k];
                        if (map[v] < 0)
                        {
                            map[v] = static_cast<int>(out.vertices.size());
                            out.vertices.push_back(m_points[v]);
                        }
                        tri[k] = map[v];
                    }
                    out.triangles.push_back(tri);
                }
                result.trianglesAfter = out.triangles.size();
                return out;
            }

            DecimateOptions m_options;
            std::vector<Point3> m_points;
            std::vector<std::array<int, 3>> m_triangles;
            std::vector<bool> m_alive;
            size_t m_aliveCount = 0;
            std::vector<bool> m_vertexAlive;
            std::vector<int> m_version;
            std::vector<Quadric> m_quadrics;
            std::vector<std::vector<int>> m_vertexTriangles;
            std::priority_queue<Candidate> m_heap;
            std::vector<int> m_scratchA, m_scratchB;
        };
    }

    Mesh Decimate(const Mesh& mesh, const DecimateOptions& options, DecimateResult* pResult)
    {
        DecimateResult result;
        Mesh out;
        if (mesh.triangles.size() <= options.targetTriangles)
        {
            out = mesh;
            result.trianglesBefore = result.trianglesAfter = mesh.triangles.size();
        }
        else
        {
            Decimator decimator(mesh, options);
            out = decimator.Run(result);
        }
        if (pResult != nullptr)
            *pResult = result;
        return out;
    }
}
