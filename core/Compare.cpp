// Compare.cpp : nearest points on a triangle mesh.

#include "Compare.h"

#include <algorithm>
#include <cmath>
#include <execution>
#include <limits>
#include <map>
#include <tuple>
#include <numeric>

namespace scancore
{
    Point3 ClosestPointOnTriangle(const Point3& p, const Point3& a, const Point3& b, const Point3& c)
    {
        // Ericson, Real-Time Collision Detection, 5.1.5.
        const Point3 ab = b - a;
        const Point3 ac = c - a;
        const Point3 ap = p - a;
        const double d1 = Dot(ab, ap);
        const double d2 = Dot(ac, ap);
        if (d1 <= 0.0 && d2 <= 0.0)
            return a;
        const Point3 bp = p - b;
        const double d3 = Dot(ab, bp);
        const double d4 = Dot(ac, bp);
        if (d3 >= 0.0 && d4 <= d3)
            return b;
        const double vc = d1 * d4 - d3 * d2;
        if (vc <= 0.0 && d1 >= 0.0 && d3 <= 0.0)
            return (d1 - d3 > 0.0) ? a + ab * (d1 / (d1 - d3)) : a;
        const Point3 cp = p - c;
        const double d5 = Dot(ab, cp);
        const double d6 = Dot(ac, cp);
        if (d6 >= 0.0 && d5 <= d6)
            return c;
        const double vb = d5 * d2 - d1 * d6;
        if (vb <= 0.0 && d2 >= 0.0 && d6 <= 0.0)
            return (d2 - d6 > 0.0) ? a + ac * (d2 / (d2 - d6)) : a;
        const double va = d3 * d6 - d5 * d4;
        if (va <= 0.0 && (d4 - d3) >= 0.0 && (d5 - d6) >= 0.0)
            return ((d4 - d3) + (d5 - d6) > 0.0) ? b + (c - b) * ((d4 - d3) / ((d4 - d3) + (d5 - d6))) : b;
        const double sum = va + vb + vc;
        if (!(std::fabs(sum) > 1e-300))
            return a;
        const double v = vb / sum;
        const double w = vc / sum;
        return a + ab * v + ac * w;
    }

    TriangleGrid::TriangleGrid(const Mesh& mesh, double cell) : m_mesh(mesh)
    {
        const Box3 box = Bounds(mesh);
        m_normals.resize(mesh.triangles.size());
        for (size_t t = 0; t < mesh.triangles.size(); ++t)
        {
            const auto& tri = mesh.triangles[t];
            const Point3& a = mesh.vertices[static_cast<size_t>(tri[0])];
            const Point3& b = mesh.vertices[static_cast<size_t>(tri[1])];
            const Point3& c = mesh.vertices[static_cast<size_t>(tri[2])];
            m_normals[t] = Normalised(Cross(b - a, c - a));
        }
        if (box.Empty())
        {
            m_start.assign(2, 0);
            return;
        }
        const Point3 size = box.Size();
        const double kMaxCells = 4.0e6;
        m_cell = std::max(cell, 1e-6);
        const double volume = (size.x + m_cell) * (size.y + m_cell) * (size.z + m_cell);
        if (volume / (m_cell * m_cell * m_cell) > kMaxCells)
            m_cell = std::cbrt(volume / kMaxCells) * 1.01;
        m_low = box.low;
        m_count[0] = std::max(1, static_cast<int>(std::floor(size.x / m_cell)) + 1);
        m_count[1] = std::max(1, static_cast<int>(std::floor(size.y / m_cell)) + 1);
        m_count[2] = std::max(1, static_cast<int>(std::floor(size.z / m_cell)) + 1);
        const size_t cells = static_cast<size_t>(m_count[0]) * m_count[1] * m_count[2];

        // Each triangle goes in every cell its box touches; counted, then filled.
        auto cellRange = [&](size_t t, int lo[3], int hi[3]) {
            const auto& tri = mesh.triangles[t];
            Box3 b;
            for (int k = 0; k < 3; ++k)
                b.Add(mesh.vertices[static_cast<size_t>(tri[k])]);
            const double l[3] = { b.low.x - m_low.x, b.low.y - m_low.y, b.low.z - m_low.z };
            const double h[3] = { b.high.x - m_low.x, b.high.y - m_low.y, b.high.z - m_low.z };
            for (int k = 0; k < 3; ++k)
            {
                lo[k] = std::clamp(static_cast<int>(std::floor(l[k] / m_cell)), 0, m_count[k] - 1);
                hi[k] = std::clamp(static_cast<int>(std::floor(h[k] / m_cell)), 0, m_count[k] - 1);
            }
        };
        std::vector<uint32_t> counts(cells + 1, 0);
        for (size_t t = 0; t < mesh.triangles.size(); ++t)
        {
            int lo[3], hi[3];
            cellRange(t, lo, hi);
            for (int k = lo[2]; k <= hi[2]; ++k)
                for (int j = lo[1]; j <= hi[1]; ++j)
                    for (int i = lo[0]; i <= hi[0]; ++i)
                        ++counts[static_cast<size_t>(CellIndex(i, j, k))];
        }
        m_start.assign(cells + 1, 0);
        for (size_t c = 0; c < cells; ++c)
            m_start[c + 1] = m_start[c] + counts[c];
        m_items.resize(m_start[cells]);
        std::vector<uint32_t> fill(m_start.begin(), m_start.end() - 1);
        for (size_t t = 0; t < mesh.triangles.size(); ++t)
        {
            int lo[3], hi[3];
            cellRange(t, lo, hi);
            for (int k = lo[2]; k <= hi[2]; ++k)
                for (int j = lo[1]; j <= hi[1]; ++j)
                    for (int i = lo[0]; i <= hi[0]; ++i)
                        m_items[fill[static_cast<size_t>(CellIndex(i, j, k))]++] = static_cast<int>(t);
        }
    }

    TriangleGrid::Nearest TriangleGrid::Find(const Point3& p, double maxDistance) const
    {
        Nearest best;
        if (m_items.empty() || !(maxDistance > 0.0))
            return best;
        const double rel[3] = { (p.x - m_low.x) / m_cell, (p.y - m_low.y) / m_cell, (p.z - m_low.z) / m_cell };
        int centre[3];
        for (int k = 0; k < 3; ++k)
        {
            const double f = std::floor(rel[k]);
            centre[k] = (f < -1e9) ? -1000000000 : (f > 1e9 ? 1000000000 : static_cast<int>(f));
        }
        // How far p is outside the grid, so rings that can't reach it are skipped.
        int skip = 0;
        for (int k = 0; k < 3; ++k)
        {
            if (centre[k] < 0)
                skip = std::max(skip, -centre[k] - 1);
            else if (centre[k] >= m_count[k])
                skip = std::max(skip, centre[k] - m_count[k]);
        }
        const int rings = static_cast<int>(std::ceil(maxDistance / m_cell)) + 1;
        const double maxSquared = maxDistance * maxDistance;
        double bestSquared = maxSquared;
        double bestSquare = -1.0;      // how squarely p is in front of or behind the best triangle

        auto visit = [&](int i, int j, int k) {
            if (i < 0 || j < 0 || k < 0 || i >= m_count[0] || j >= m_count[1] || k >= m_count[2])
                return;
            const int64_t c = CellIndex(i, j, k);
            for (uint32_t n = m_start[static_cast<size_t>(c)]; n < m_start[static_cast<size_t>(c) + 1]; ++n)
            {
                const int t = m_items[n];
                const auto& tri = m_mesh.triangles[static_cast<size_t>(t)];
                const Point3 q = ClosestPointOnTriangle(p, m_mesh.vertices[static_cast<size_t>(tri[0])],
                                                        m_mesh.vertices[static_cast<size_t>(tri[1])],
                                                        m_mesh.vertices[static_cast<size_t>(tri[2])]);
                const Point3 d = p - q;
                const double squared = Dot(d, d);
                if (!std::isfinite(squared))
                    continue;      // a degenerate triangle
                if (!best.found && squared > maxSquared)
                    continue;
                const double length = std::sqrt(squared);
                const Point3& normal = m_normals[static_cast<size_t>(t)];
                const double square = (length > 1e-12) ? std::fabs(Dot(d, normal)) / length : 1.0;
                if (best.found)
                {
                    const double tolerance = 1e-9 * (1.0 + bestSquared);
                    if (squared > bestSquared + tolerance)
                        continue;
                    if (squared >= bestSquared - tolerance && square <= bestSquare)
                        continue;       // as near, but no squarer
                }
                best.found = true;
                best.point = q;
                best.triangle = t;
                best.distance = length;
                best.signedDistance = (Dot(d, normal) >= 0.0) ? length : -length;
                bestSquare = square;
                bestSquared = best.found ? std::min(bestSquared, squared) : squared;
            }
        };

        for (int r = skip; r <= rings; ++r)
        {
            // Everything in ring r is at least (r - 1) cells away.
            if (best.found && static_cast<double>(r - 1) * m_cell > best.distance)
                break;
            if (static_cast<double>(r - 1) * m_cell > maxDistance)
                break;
            for (int dk = -r; dk <= r; ++dk)
                for (int dj = -r; dj <= r; ++dj)
                {
                    const bool face = (std::abs(dk) == r || std::abs(dj) == r);
                    if (face)
                    {
                        for (int di = -r; di <= r; ++di)
                            visit(centre[0] + di, centre[1] + dj, centre[2] + dk);
                    }
                    else
                    {
                        visit(centre[0] - r, centre[1] + dj, centre[2] + dk);
                        if (r > 0)
                            visit(centre[0] + r, centre[1] + dj, centre[2] + dk);
                    }
                }
        }
        if (best.found && best.distance > maxDistance)
            best = Nearest();
        return best;
    }

    std::vector<float> Deviations(const std::vector<Point3>& points, const Mesh& part, double range)
    {
        std::vector<float> values(points.size(), std::numeric_limits<float>::quiet_NaN());
        if (part.triangles.empty() || !(range > 0.0))
            return values;
        const TriangleGrid grid(part, range / 2.0);
        std::vector<size_t> order(points.size());
        std::iota(order.begin(), order.end(), size_t{ 0 });
        std::for_each(std::execution::par, order.begin(), order.end(), [&](size_t i) {
            const TriangleGrid::Nearest n = grid.Find(points[i], range);
            if (n.found)
                values[i] = static_cast<float>(n.signedDistance);
        });
        return values;
    }

    Mesh MeshFromFacets(const std::vector<double>& values, double millimetresPerUnit)
    {
        Mesh mesh;
        std::map<std::tuple<long long, long long, long long>, int> joined;
        auto corner = [&](size_t at) {
            const Point3 p{ values[at] * millimetresPerUnit, values[at + 1] * millimetresPerUnit,
                            values[at + 2] * millimetresPerUnit };
            const auto key = std::make_tuple(std::llround(p.x * 1e6), std::llround(p.y * 1e6), std::llround(p.z * 1e6));
            const auto found = joined.find(key);
            if (found != joined.end())
                return found->second;
            const int index = static_cast<int>(mesh.vertices.size());
            mesh.vertices.push_back(p);
            joined.emplace(key, index);
            return index;
        };
        for (size_t i = 0; i + 8 < values.size(); i += 9)
        {
            bool finite = true;
            for (size_t k = 0; k < 9; ++k)
                finite = finite && std::isfinite(values[i + k]);
            if (!finite)
                continue;
            const std::array<int, 3> tri{ corner(i), corner(i + 3), corner(i + 6) };
            if (tri[0] != tri[1] && tri[1] != tri[2] && tri[0] != tri[2])
                mesh.triangles.push_back(tri);
        }
        double volume = 0.0;
        for (const auto& t : mesh.triangles)
            volume += Dot(mesh.vertices[static_cast<size_t>(t[0])],
                          Cross(mesh.vertices[static_cast<size_t>(t[1])], mesh.vertices[static_cast<size_t>(t[2])]));
        if (volume < 0.0)
            for (auto& t : mesh.triangles)
                std::swap(t[1], t[2]);
        return mesh;
    }

    DeviationStats DeviationStatistics(const std::vector<float>& values, double tolerance)
    {
        DeviationStats stats;
        double sumSquared = 0.0;
        size_t within = 0;
        for (float value : values)
        {
            if (std::isnan(value))
            {
                ++stats.notCompared;
                continue;
            }
            ++stats.compared;
            stats.largestPositive = std::max(stats.largestPositive, static_cast<double>(value));
            stats.largestNegative = std::min(stats.largestNegative, static_cast<double>(value));
            sumSquared += static_cast<double>(value) * value;
            within += (std::fabs(value) <= tolerance) ? 1 : 0;
        }
        if (stats.compared > 0)
        {
            stats.rms = std::sqrt(sumSquared / static_cast<double>(stats.compared));
            stats.within = static_cast<double>(within) / static_cast<double>(stats.compared);
        }
        return stats;
    }

    int DeviationBand(double value, double tolerance, double range)
    {
        if (std::isnan(value) || std::fabs(value) > range)
            return kDeviationGrey;
        if (std::fabs(value) <= tolerance)
            return 3;
        if (value < 0.0)
            return (value < -2.0 * range / 3.0) ? 0 : (value < -range / 3.0 ? 1 : 2);
        return (value > 2.0 * range / 3.0) ? 6 : (value > range / 3.0 ? 5 : 4);
    }
}
